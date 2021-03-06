// Copyright 2009-2017 Sandia Corporation. Under the terms
// of Contract DE-NA0003525 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2017, Sandia Corporation
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

// Copyright 2015 IBM Corporation

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//   http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sst_config.h"

#include "c_ControllerPCA.hpp"
#include "c_TxnReqEvent.hpp"
#include "c_TxnResEvent.hpp"
#include "c_CmdResEvent.hpp"


using namespace SST;
using namespace SST::n_Bank;
using namespace SST::CACHE;

c_ControllerPCA::c_ControllerPCA(ComponentId_t id, Params &x_params) :
        c_Controller(id, x_params){

    int verbosity = x_params.find<int>("verbose", 0);
    output = new SST::Output("CramSim.Controller[@f:@l:@p] ",
                             verbosity, 0, SST::Output::STDOUT);

    bool l_found;

    m_osPageSize = x_params.find<uint64_t>("page_size",4096,l_found);
    if(l_found==false)
    {
        fprintf(stderr,"[C_ControllerPCA] OS Page Size is not specified: it will be 4096\n");
    }

    loopback_en = x_params.find<bool>("loopback_en",false,l_found);
    if(loopback_en==true)
    {
        fprintf(stderr,"[C_ControllerPCA] Loopback mode is enabled\n");
    }

    compression_en= x_params.find<bool>("compression_en",false,l_found);
    if(compression_en==true)
    {
        fprintf(stderr,"[C_ControllerPCA] compression is enabled\n");
    }

    int contentline_num= x_params.find<int>("contentline_num",1,l_found);
    if(l_found==false)

    {
        fprintf(stderr,"[C_ControllerPCA] contentline_num is miss\n");
    }

    pca_mode=x_params.find<bool>("pca_enable",false);
    oracle_mode=x_params.find<bool>("oracle_mode",false);
    memzip_mode=x_params.find<bool>("memzip_mode",false);
    m_metacache_latency=x_params.find<uint64_t>("metacache_latency",3);  //3 cycle is 8 cpu cycles at cpu 4GHz and memory 1.6GHz
    multilane_rowtable=x_params.find<bool>("multilane_rowtable",false); // enable multilane per each entry of row table

    if(memzip_mode)
    {
        printf("memzip mode is enabled\n");
    }

    bool compression_engine_validation=x_params.find<bool>("comp_engine_validation",false);
    m_isFixedCompressionMode=x_params.find<bool>("fixed_compression_mode",false);

    if(m_isFixedCompressionMode){
        uint32_t compression_data_rate=x_params.find<uint32_t>("compression_data_rate",50,l_found);
        if(!l_found)
        {
            fprintf(stderr,"[C_ControllerPCA] compression_data_rate is miss.. exit..");
            exit(-1);
        }
        else
            m_compEngine = new c_CompressEngine(compression_data_rate);

    }
    else
        m_compEngine = new c_CompressEngine(verbosity,compression_engine_validation);


    metadata_predictor= x_params.find<int>("metadata_predictor",0);
    no_metadata = x_params.find<bool>("no_metadata",false);

    if(metadata_predictor==1)       //meta-data cache
    {
        uint32_t metacache_row_num = x_params.find<uint32_t>("metacache_rownum",32,l_found); //128KB, 130b per entry

        uint32_t dram_rowsize=m_deviceDriver->getNumColPerBank()*64;
        double hitrate= x_params.find<double>("metacache_hitrate",0,l_found);
        int metacache_way= x_params.find<int>("metacache_way",16,l_found);
        MCache_ReplPolicy_Enum metacache_rpl_policy = (MCache_ReplPolicy_Enum)x_params.find<int>("metacache_rpl_policy",MCache_ReplPolicy_Enum::REPL_LRU);
        //metacache = new c_MetaCache(dram_rowsize,metacache_row_num,metacache_way,output,hitrate);
        metacache = new SCache(metacache_row_num,metacache_way,metacache_rpl_policy,m_osPageSize);
        m_metacache_update_cnt=0;
        m_mcache_wb_cnt=0;
        m_mcache_evict_cnt=0;
    }
    else if(metadata_predictor==2) //compression predictor
    {
        bool found=false;

        uint32_t ropr_entry_num = x_params.find<uint32_t>("ropr_entry_num",512*1024,found); //128KB, 80b per entry
        uint32_t lipr_entry_num = x_params.find<uint32_t>("lipr_entry_num",8*1024,found); //128KB, 80b per entry
        int lipr_entry_colnum = x_params.find<uint32_t>("lipr_entry_colnum",128,found); //128KB, 130b per entry
        bool selectiveRepl= x_params.find<bool>("selectiveRepl",true,found); //128KB, 130b per entry

        cmpSize_predictor = new c_2LvPredictor(ropr_entry_num,lipr_entry_num,lipr_entry_colnum,selectiveRepl,output);
    }
    else if(metadata_predictor==3) //new compression predictor
    {
         bool found=false;

        uint32_t ropr_entry_num = x_params.find<uint32_t>("ropr_entry_num",64*1024,found); //256KB, 16way, 2b per entry
        uint32_t global_entry_num = x_params.find<uint32_t>("global_entry_num",32,found); //32KB, 8bit per entry
        uint32_t global_pred_threshold = x_params.find<uint32_t>("global_pred_thres",100,found); //global prediction threshold
        uint32_t ropr_col_num = x_params.find<uint32_t>("ropr_col_num",1,found); //global prediction threshold

        cmpSize_predictor_new = new c_2LvPredictor_new(global_entry_num,ropr_entry_num,ropr_col_num,m_osPageSize,global_pred_threshold ,output);
    }

    isMultiThreadMode=x_params.find<bool>("multiThreadMode",false);
    /*---- CONFIGURE LINKS ----*/

   // m_contentLink = configureLink( "contentLink",new Event::Handler<c_ControllerPCA>(this,&c_ControllerPCA::handleContentEvent) );


   s_CompRatio = registerStatistic<double>("compRatio");
    s_simCycles= registerStatistic<uint64_t>("simCycles");
   s_RowSize0  = registerStatistic<uint64_t>("rowsize0_cnt");
   s_RowSize25 = registerStatistic<uint64_t>("rowsize25_cnt");
   s_RowSize50 = registerStatistic<uint64_t>("rowsize50_cnt");
   s_RowSize75 = registerStatistic<uint64_t>("rowsize75_cnt");
    s_RowSize100 = registerStatistic<uint64_t>("rowsize100_cnt");
    s_BackingMiss = registerStatistic<uint64_t>("backing_store_miss");
    s_DoubleRankAccess = registerStatistic<uint64_t>("double_rank_access");
    s_SingleRankAccess = registerStatistic<uint64_t>("single_rank_access");
    s_MemzipMetaCacheHit = registerStatistic<uint64_t>("memzip_metacache_hit");
    s_MemzipMetaCacheMiss = registerStatistic<uint64_t>("memzip_metacache_miss");
    s_CachelineSize50 = registerStatistic<uint64_t>("cacheline_size_50");
    s_CachelineSize100 = registerStatistic<uint64_t>("cacheline_size_100");
    s_CachelineSize100 = registerStatistic<uint64_t>("cacheline_size_100");

   s_predicted_fail_below50=registerStatistic<uint64_t>("predicted_fail_below50");
   s_predicted_success_above50=registerStatistic<uint64_t>("predicted_success_above50");
   s_predicted_success_below50=registerStatistic<uint64_t>("predicted_success_below50");
   s_predicted_fail_above50=registerStatistic<uint64_t>("predicted_fail_above50");
    s_predictor_ropr_hit = registerStatistic<uint64_t>("predictor_ropr_hit");
   s_predictor_ropr_miss = registerStatistic<uint64_t>("predictor_ropr_miss");
   s_predictor_lipr_hit = registerStatistic<uint64_t>("predictor_lipr_hit");
   s_predictor_lipr_miss = registerStatistic<uint64_t>("predictor_lipr_miss");
   s_predictor_lipr_success=registerStatistic<uint64_t>("predictor_lipr_success");
   s_predictor_lipr_fail=registerStatistic<uint64_t>("predictor_lipr_fail");
   s_predictor_ropr_success=registerStatistic<uint64_t>("predictor_ropr_success");
   s_predictor_ropr_fail=registerStatistic<uint64_t>("predictor_ropr_fail");
    s_predictor_global_success=registerStatistic<uint64_t>("predictor_global_success");
   s_predictor_global_fail=registerStatistic<uint64_t>("predictor_global_fail");
    s_metacache_data_update=registerStatistic<uint64_t>("metacache_data_update");
    s_metacache_evict=registerStatistic<uint64_t>("metacache_evict");
    s_metacache_wb=registerStatistic<uint64_t>("metacache_wb");

    //---- configure link ----//

    for (int i = 0; i < contentline_num; i++) {
        string l_linkName = "lane_" + to_string(i);
        Link *l_link = configureLink(l_linkName);

        if (l_link) {
            m_laneLinks.push_back(l_link);
            cout<<l_linkName<<" is connected"<<endl;
        } else {
            cout<<l_linkName<<" is not found.. exit"<<endl;
            exit(-1);
        }
    }

    m_nextPageAddress=0;

    m_total_num_banks= m_deviceDriver->getTotalNumBank();
    m_chnum=m_deviceDriver->getNumChannel();
    m_ranknum=m_deviceDriver->getNumRanksPerChannel();
    m_bgnum=m_deviceDriver->getNumBankGroupsPerRank();
    m_banknum=m_deviceDriver->getNumBanksPerBankGroup();
    m_rownum=m_deviceDriver->getNumRowsPerBank();
    m_colnum=m_deviceDriver->getNumColPerBank();
    m_memsize=m_chnum*m_ranknum*m_bgnum*m_banknum*m_rownum*m_colnum*64;
}

void c_ControllerPCA::init(unsigned int phase){
  /*  if(phase==0)
       if(m_contentLink)
            m_contentLink->sendInitData(new MemHierarchy::MemEventInit(this->getName(),MemHierarchy::MemEventInit::InitCommand::Region));
*/
   }


void c_ControllerPCA::finish() {
    uint64_t cnt=0;
    uint64_t normalized_size_sum=0;
    uint64_t compressed_size_50=0;
    uint64_t compressed_size_100=0;
    for(int i=0;i<=50;i++)
    {
        compressed_size_50+=m_normalized_size[i];
    }

     for(int i=51;i<=100;i++)
    {
        compressed_size_100+=m_normalized_size[i];
    }
    for(int i=0;i<100;i++)
    {
        printf("compressed_size_count: %d/%lld\n",i,m_normalized_size[i]);
        normalized_size_sum+=i*m_normalized_size[i];
        cnt+=m_normalized_size[i];
    }
    printf("compressed_size<=50 : %lld\n",compressed_size_50);
    printf("compressed_size>50 : %lld\n",compressed_size_100);
    s_CachelineSize50->addData(compressed_size_50);
    s_CachelineSize100->addData(compressed_size_100);

    double avg_normalized_size = (double)normalized_size_sum / (double)cnt;
    double compression_ratio = (double)1/(double)avg_normalized_size*100;

    int total_banks=m_deviceDriver->getTotalNumBank();

    int rows    = m_deviceDriver->getNumRowsPerBank();
    int chs     = m_deviceDriver->getNumChannel();
    int ranks   = m_deviceDriver->getNumRanksPerChannel();
    int bgs     = m_deviceDriver->getNumBankGroupsPerRank();
    int banks   = m_deviceDriver->getNumBanksPerBankGroup();

    uint64_t size_25=0;
    uint64_t size_50=0;
    uint64_t size_75=0;
    uint64_t size_100=0;
    uint64_t size_0=0;

    uint64_t weighted_size_25=0;
    uint64_t weighted_size_50=0;
    uint64_t weighted_size_75=0;
    uint64_t weighted_size_100=0;
    uint64_t weighted_size_0=0;
    uint64_t accessed_row_cnt=0;

    printf("the compressed size distribution of row\n");
    printf("ch\trank\tbg\tbank\trow\t25%\t50%\t75%\t100%\n");
    int bankid=0;

    for(int ch=0;ch<chs;ch++)
        for(int rank=0;rank<ranks;rank++)
            for(int bg=0;bg<bgs;bg++)
                for(int bank=0;bank<banks;bank++)
                {
                    for(int row=0;row<rows;row++)
                    {
                        if(m_row_stat.find(bankid)!=m_row_stat.end())
                        {
                            if(m_row_stat[bankid].find(row)!=m_row_stat[bankid].end()) {

                                uint64_t cnt25 = 0;
                                uint64_t cnt50 = 0;
                                uint64_t cnt75 = 0;
                                uint64_t cnt100 = 0;

                                uint64_t access_cnt = m_row_stat[bankid][row]->get_access_cnt();
                                if (access_cnt > 0) {

                                    cnt25 = m_row_stat[bankid][row]->get_normalized_size_cnt(25);

                                    cnt50 = m_row_stat[bankid][row]->get_normalized_size_cnt(50);

                                    cnt75 = m_row_stat[bankid][row]->get_normalized_size_cnt(75);

                                    cnt100 = m_row_stat[bankid][row]->get_normalized_size_cnt(100);

                                    printf("%d\t%d\t%d\t%d\t%d\t%lld\t%lld\t%lld\t%lld\n", ch, rank, bg, bank, row, cnt25,
                                           cnt50, cnt75, cnt100);

                                    if (cnt100 > 0)
                                        size_100++;
                                    else if (cnt75 > 0)
                                        size_75++;
                                    else if (cnt50 > 0)
                                        size_50++;
                                    else if (cnt25++)
                                        size_25++;
                                    else
                                        size_0++;

                                    if (cnt100 > 0)
                                        weighted_size_100 += access_cnt;
                                    else if (cnt75 > 0)
                                        weighted_size_75 += access_cnt;
                                    else if (cnt50 > 0)
                                        weighted_size_50 += access_cnt;
                                    else if (cnt25++)
                                        weighted_size_25 += access_cnt;
                                    else
                                        weighted_size_0 += access_cnt;
                                }
                            }
                        }

                    }
                    bankid++;
                }

    s_RowSize0->addData(size_0);
    s_RowSize25->addData(size_25);
    s_RowSize50->addData(size_50);
    s_RowSize75->addData(size_75);
    s_RowSize100->addData(size_100);
    s_CompRatio->addData(compression_ratio);
    s_simCycles->addData(m_simCycle);
    s_metacache_data_update->addData(m_metacache_update_cnt);
    s_metacache_evict->addData(m_mcache_evict_cnt);
    s_metacache_wb->addData(m_mcache_wb_cnt);

    if(metacache&&metadata_predictor==1) {
        /*s_MemzipMetaCacheHit->addData(metacache->getHitCnt());
        s_MemzipMetaCacheMiss->addData(metacache->getMissCnt());
        printf("memzip metacache hit: %lld\n",metacache->getHitCnt());
        printf("memzip metacache miss: %lld\n",metacache->getMissCnt());*/
        s_MemzipMetaCacheHit->addData(metacache->getAccessCnt()-metacache->getMissCnt());
        s_MemzipMetaCacheMiss->addData(metacache->getMissCnt());
        printf("memzip metacache hit: %lld\n",metacache->getAccessCnt()-metacache->getMissCnt());
        printf("memzip metacache miss: %lld\n",metacache->getMissCnt());
    }
    else if((cmpSize_predictor&&metadata_predictor==2))
    {
        printf("predictor metacache row hit: %lld\n",cmpSize_predictor->getHitCnt());
        printf("predictor metacache row miss: %lld\n",cmpSize_predictor->getMissCnt());
        printf("predictor metacache cacheline hit: %lld\n",cmpSize_predictor->getClHitCnt());
        printf("predictor metacache cacheline miss: %lld\n",cmpSize_predictor->getClMissCnt());
        printf("predictor prediction success: %lld\n",cmpSize_predictor->getPredSuccessCnt());
        printf("predictor prediction fail: %lld\n",cmpSize_predictor->getPredFailCnt());
        s_predictor_lipr_hit->addData(cmpSize_predictor->m_predictor_lipr_hit);
        s_predictor_lipr_miss->addData(cmpSize_predictor->m_predictor_lipr_miss);
        s_predictor_lipr_success->addData(cmpSize_predictor->m_predictor_lipr_success);
        s_predictor_lipr_fail->addData(cmpSize_predictor->m_predictor_lipr_fail);
        s_predictor_ropr_success->addData(cmpSize_predictor->m_predictor_ropr_success);
        s_predictor_ropr_fail->addData(cmpSize_predictor->m_predictor_ropr_fail);
    }// new predictor that has set-associated cache structure
    else if((cmpSize_predictor_new&&metadata_predictor==3))
    {
        s_predictor_ropr_hit->addData(cmpSize_predictor_new->m_predictor_ropr_hit);
        s_predictor_ropr_miss->addData(cmpSize_predictor_new->m_predictor_ropr_miss);

        s_predictor_lipr_hit->addData(cmpSize_predictor_new->m_predictor_lipr_hit);
        s_predictor_lipr_miss->addData(cmpSize_predictor_new->m_predictor_lipr_miss);

        s_predictor_lipr_success->addData(cmpSize_predictor_new->m_predictor_lipr_success);
        s_predictor_lipr_fail->addData(cmpSize_predictor_new->m_predictor_lipr_fail);

        s_predictor_ropr_success->addData(cmpSize_predictor_new->m_predictor_ropr_success);
        s_predictor_ropr_fail->addData(cmpSize_predictor_new->m_predictor_ropr_fail);

        s_predictor_global_success->addData(cmpSize_predictor_new->m_predictor_global_success);
        s_predictor_global_fail->addData(cmpSize_predictor_new->m_predictor_global_fail);
    }

    printf("cacheline compression ratio:\t%lf\n",compression_ratio);
    printf("row size0:\t%lld\n",size_0);
    printf("row size25:\t%lld\n",size_25);
    printf("row size50:\t%lld\n",size_50);
    printf("row size75:\t%lld\n",size_75);
    printf("row size100:\t%lld\n",size_100);
    printf("weighted_row size0:\t%lld\n",weighted_size_0);
    printf("weighted_row size25:\t%lld\n",weighted_size_25);
    printf("weighted_row size50:\t%lld\n",weighted_size_50);
    printf("weighted_row size75:\t%lld\n",weighted_size_75);
    printf("weighted_row size100:\t%lld\n",weighted_size_100);
}


// clock event handler
bool c_ControllerPCA::clockTic(SST::Cycle_t clock) {
    storeContent();
    m_simCycle++;

   // sendResponse();

    // 0. update device driver
    m_deviceDriver->update();

    // 1. Convert physical address to device address
    // 2. Push transactions to the transaction queue
    for(std::deque<c_Transaction*>::iterator l_it=m_ReqQ.begin() ; l_it!=m_ReqQ.end();)
    {
        c_Transaction* newTxn= *l_it;

        if(!(newTxn->m_time_arrived_Controller+m_metacache_latency<m_simCycle))
            break;

        if(newTxn->hasHashedAddress()== false)
        {
            c_HashedAddress l_hashedAddress;
            m_addrHasher->fillHashedAddress(&l_hashedAddress, newTxn->getAddress());
            newTxn->setHashedAddress(l_hashedAddress);
        }

        uint64_t addr = (newTxn->getAddress() >> 6) << 6;
        //calculate the compressed size of cacheline
        if((m_isFixedCompressionMode||compRatio_bdi.size()>0)&&newTxn->getCompressedSize()<0) {

                if(!m_isFixedCompressionMode &&(compRatio_bdi.find(addr)==compRatio_bdi.end())) {
                    printf("Error!! cacheline is not found, %llx\n",addr);
                    compRatio_bdi[addr] = 0;
                    s_BackingMiss->addData(1);
                }

                uint8_t normalized_size = 100;

                if(!m_isFixedCompressionMode)
                    normalized_size=compRatio_bdi[addr];
                else
                    normalized_size= m_compEngine->getCompressedSize();

                m_normalized_size[normalized_size]++;

                newTxn->setCompressedSize(normalized_size);

                int bankid = newTxn->getHashedAddress().getBankId();
                uint32_t row = newTxn->getHashedAddress().getRow();
                int col = newTxn->getHashedAddress().getCol();


        }

        if(loopback_en==true) {
            newTxn->setResponseReady();

            /*if (metacache->isHit(addr)){
            }
            else
            {
                metacache->fill(addr);
            }*/

            l_it=m_ReqQ.erase(l_it);
            sendResponse(newTxn);

            if ((m_ResQ.size() > 0)) {
                c_Transaction* l_txnRes = nullptr;
                for (std::deque<c_Transaction*>::iterator l_it = m_ResQ.begin();
                     l_it != m_ResQ.end();)  {
                    if ((*l_it)->isResponseReady()) {
                        l_txnRes = *l_it;
                        l_it=m_ResQ.erase(l_it);
                        l_txnRes->print(output,"delete txn",m_simCycle);

                        c_TxnResEvent* l_txnResEvPtr = new c_TxnResEvent();
                        l_txnResEvPtr->m_payload = l_txnRes;

                        m_txngenLink->send(l_txnResEvPtr);
                    }
                    else
                    {
                        l_it++;
                    }
                }
            }
            continue;
        }



        //partial-chip access
        else if(pca_mode) {
             assert(m_deviceDriver->getNumPChPerChannel() == 1);

             //determine subrank
             uint32_t row = newTxn->getHashedAddress().getRow();
             unsigned pch = row & 0x1;
             int Chs = m_deviceDriver->getNumChannel();
             int PChs = m_deviceDriver->getNumPChPerChannel();
             int Ranks = m_deviceDriver->getNumRanksPerChannel();
             int BGs = m_deviceDriver->getNumBankGroupsPerRank();
             int Banks = m_deviceDriver->getNumBanksPerBankGroup();

             c_HashedAddress l_hashedAddress = newTxn->getHashedAddress();
             l_hashedAddress.setPChannel(pch);

             unsigned l_bankId =
                     l_hashedAddress.getBank()
                     + l_hashedAddress.getBankGroup() * Banks
                     + l_hashedAddress.getRank() * Banks * BGs
                     + l_hashedAddress.getPChannel() * Banks * BGs * Ranks
                     + l_hashedAddress.getChannel() * (PChs+1) * Banks * BGs * (Ranks );
             //std::cout <<"row: "<<row<<" subrank: "<<subrank<<" bankid[old]: "<<l_hashedAddress.getBankId()<< " bankid[new]: "<<l_bankId<<std::endl;
             unsigned l_rankId =
                     +l_hashedAddress.getRank()
                     + l_hashedAddress.getPChannel() * Ranks
                     + l_hashedAddress.getChannel() * (PChs+1) * (Ranks );



             l_hashedAddress.setRankId(l_rankId);
             l_hashedAddress.setBankId(l_bankId);
             newTxn->setHashedAddress(l_hashedAddress);


             bool isNeedHelper = false;


             if (!newTxn->isMetaDataSkip()) {
                 newTxn->setMetaDataSkip();
                 switch (metadata_predictor) {
                     case 0: //perfect predictor
                     {
                         isNeedHelper = newTxn->needHelper();
                         newTxn->setChipAccessRatio(newTxn->getCompressedSize());
                         break;
                     }
                     case 1:  //memzip metacache
                     {
                         assert(metacache != NULL);
                         uint64_t cl_addr = (addr>>6)<<6;

                         bool metacache_data=false;
                         bool isCompressible=false;
                         bool doUpdate_metadata_cache= false;

                         //check compressibility of current cacheline
                         if(newTxn->getCompressedSize()<=50)
                             isCompressible=true;
                         else
                             isCompressible=false;

                         //read metacache data
                         if(m_metacache_data.find(cl_addr)==m_metacache_data.end()) {
                             m_metacache_data[cl_addr] = isCompressible;
                             metacache_data=false;
                         } else
                             metacache_data=m_metacache_data[cl_addr];


                         if(isCompressible!=metacache_data) {
                             m_metacache_data[cl_addr]=isCompressible;
                             if(newTxn->isWrite())
                                 doUpdate_metadata_cache = true;
                         }
                         else
                             doUpdate_metadata_cache=false;

//                         printf("addr:%llx cl_addr:%llx pagenum:%lld iswrite:%d compsize:%d isCompressible:%d metacache_data:%d doUpate metadata cache:%d update count:%lld mcache_evict_cnt:%lld mcache_wb_cnt:%lld\n",
  //                              addr,cl_addr,(cl_addr >>12)<<12, newTxn->isWrite(),newTxn->getCompressedSize(),isCompressible, metacache_data, doUpdate_metadata_cache,m_metacache_update_cnt,m_mcache_evict_cnt,m_mcache_wb_cnt);
                         if (metacache->isHit(addr,doUpdate_metadata_cache)) {

                             isNeedHelper = newTxn->needHelper();
                             int chip_access_ratio=100;
                             if(newTxn->getCompressedSize()<=50)
                                 chip_access_ratio=50;
                             else
                                 chip_access_ratio=100;

                             uint64_t cl_addr = (addr>>6)<<6;

                             newTxn->setChipAccessRatio(chip_access_ratio);

                             if(doUpdate_metadata_cache) {
                                 m_metacache_update_cnt++;
                             }
                           //  printf("addr:%llx hit access_cnt:%lld miss_cnt:%lld\n",addr,metacache->getAccessCnt(),metacache->getMissCnt());
                         } else {
                             //access all chip
                             isNeedHelper = true;
                             newTxn->setCompressedSize(100);
                             newTxn->setChipAccessRatio(100);

                             //write back metadata if dirty
                             MCache_Entry victim = metacache->install(addr,doUpdate_metadata_cache);
                             m_mcache_evict_cnt++;

                             if(doUpdate_metadata_cache) {
                                 m_metacache_update_cnt++;
                             }

                             if(victim.dirty) {
                                 m_mcache_wb_cnt++;
                                 uint64_t writeback_addr_high = (uint64_t) rand();
                                 uint64_t writeback_addr_low = (uint64_t) rand();
                                 uint64_t writeback_addr =
                                         (writeback_addr_high << 32 | (writeback_addr_low)) & (m_memsize - 1);

                                 c_Transaction *writebackTxn
                                         = new c_Transaction(newTxn->getSeqNum(), e_TransactionType::WRITE,
                                                             writeback_addr,
                                                             1);

                                 c_HashedAddress l_hashedAddress2;
                                 m_addrHasher->fillHashedAddress(&l_hashedAddress2, writeback_addr);

                                 //determine subrank
                                 uint32_t row2 = l_hashedAddress2.getRow();
                                 int Chs2 = m_deviceDriver->getNumChannel();
                                 int Pchs2 = m_deviceDriver->getNumPChPerChannel();
                                 int Ranks2 = m_deviceDriver->getNumRanksPerChannel();
                                 int BGs2 = m_deviceDriver->getNumBankGroupsPerRank();
                                 int Banks2 = m_deviceDriver->getNumBanksPerBankGroup();

                                 unsigned pch = row2 & 0x1;
                                 l_hashedAddress2.setPChannel(pch);


                                 unsigned l_bankId =
                                         l_hashedAddress2.getBank()
                                         + l_hashedAddress2.getBankGroup() * Banks2
                                         + l_hashedAddress2.getRank() * Banks2 * BGs2
                                         + l_hashedAddress2.getPChannel() * Banks2 * BGs2 * Ranks2
                                         + l_hashedAddress2.getChannel() * (Pchs2 + 1) * Banks2 * BGs2 * (Ranks2);

                                 unsigned l_rankId =
                                         +l_hashedAddress2.getRank()
                                         + l_hashedAddress2.getPChannel() * Ranks2
                                         + l_hashedAddress2.getChannel() * (Pchs2 + 1) * (Ranks2);


                                 l_hashedAddress2.setRankId(l_rankId);
                                 l_hashedAddress2.setBankId(l_bankId);
                                 writebackTxn->setHashedAddress(l_hashedAddress2);

                                 writebackTxn->setMetaDataSkip();
                                 writebackTxn->setMetaDataTxn();
                                 writebackTxn->donotRespond();

                                 writebackTxn->setCompressedSize(100);
                                 writebackTxn->setChipAccessRatio(100);

                                 m_MReqQ.push_back(writebackTxn);
                                 m_ResQ.push_back(writebackTxn);

                             }

                             //fetch metadata
                             uint64_t fetch_addr = addr;
                             c_Transaction *fillTxn
                                     = new c_Transaction((ulong) newTxn->getSeqNum(), e_TransactionType::READ,
                                                         fetch_addr, 1);

                             c_HashedAddress l_hashedAddress3 = newTxn->getHashedAddress();

                             fillTxn->setMetaDataSkip();
                             fillTxn->setHashedAddress(l_hashedAddress3);
                             fillTxn->donotRespond();
                             fillTxn->setCompressedSize(100);
                             fillTxn->setChipAccessRatio(100);
                             fillTxn->setMetaDataTxn();

                             m_MReqQ.push_back(fillTxn);
                             m_ResQ.push_back(fillTxn);

                             //printf("addr:%llx miss, metadata fill addr:%llx, metadata wb addr:%llx\n",addr,fetch_addr,writeback_addr);
                         }
                         break;
                     }
                     case 2: {
                         assert(cmpSize_predictor != NULL);
                         int col = newTxn->getHashedAddress().getCol();
                         int row = newTxn->getHashedAddress().getRow();
                         int bankid = newTxn->getHashedAddress().getBankId();
                         int rowoffset = (int) log2(m_rownum);
                         int rowid=0;
                         if(!multilane_rowtable)
                             rowid = (bankid << rowoffset) | row;
                         else {
                             int lane_idx= (col>=64) ? 1 : 0;
                             rowid = (bankid << rowoffset+1) | row<<1 | lane_idx;
                         }

                         bool isWrite = newTxn->isWrite();

                         int compSize = 100;

                         if (isWrite) {
                             compSize = newTxn->getCompressedSize();
                             isNeedHelper = newTxn->needHelper();
                         } else {
                             int predSize = cmpSize_predictor->getPredictedSize(col, rowid,
                                                                                newTxn->getCompressedSize());
                             int actualSize = newTxn->getCompressedSize();

                             if (actualSize > 50) {
                                 isNeedHelper = true;
                                 compSize = actualSize;
                                 if (predSize <= 50)
                                     s_predicted_fail_below50->addData(1);
                                 else
                                     s_predicted_success_above50->addData(1);
                             } else {
                                 if (predSize <= 50) {
                                     isNeedHelper = false;
                                     compSize = predSize;
                                     s_predicted_success_below50->addData(1);
                                 } else {
                                     isNeedHelper = true;
                                     compSize = actualSize;
                                     s_predicted_fail_above50->addData(1);
                                 }
                             }
                         }

                         cmpSize_predictor->update(col, rowid, newTxn->getCompressedSize());

                         newTxn->setChipAccessRatio(compSize);
                         newTxn->setCompressedSize(50);
                         break;
                     }
                     case 3:  //new predictor or
                     case 4:  //no predictor
                     {
                         assert(cmpSize_predictor_new != NULL);
                         int col = newTxn->getHashedAddress().getCol();
                         int row = newTxn->getHashedAddress().getRow();
                         int bankid = newTxn->getHashedAddress().getBankId();
                         int rowoffset = (int) log2(m_rownum);

                         bool isWrite = newTxn->isWrite();

                         int compSize = 100;

                         if (isWrite) {
                             compSize = newTxn->getCompressedSize();
                             isNeedHelper = newTxn->needHelper();
                         } else {
                             int actualSize = newTxn->getCompressedSize();
                             int predSize = 100;


                             if(metadata_predictor==3)
                                 //if new predictor is enabled, get the predicted size with the predictor
                                 predSize=cmpSize_predictor_new->getPredictedSize(addr,actualSize);
                             else if(metadata_predictor==4)
                                 // disabled. always predict as compressed cacheline
                                 predSize=50;



                             if (actualSize > 50) {
                                // isNeedHelper = true;
                                 compSize = actualSize;
                                 if (predSize <= 50) {
                                     s_predicted_fail_below50->addData(1);
                                     isNeedHelper = false;
                                 }
                                 else
                                 {
                                     s_predicted_success_above50->addData(1);
                                     isNeedHelper = true;
                                 }
                             } else {
                                 if (predSize <= 50) {
                                     isNeedHelper = false;
                                     compSize = predSize;
                                     s_predicted_success_below50->addData(1);
                                 } else {
                                     isNeedHelper = true;
                                     compSize = actualSize;
                                     s_predicted_fail_above50->addData(1);
                                 }
                             }
                         }

                         newTxn->setChipAccessRatio(compSize);
                         newTxn->setCompressedSize(50);

                         break;
                     }
                     default: {
                         isNeedHelper = true;
                         newTxn->setChipAccessRatio(50);
                         break;
                     }
                 }


                 //add helper transaction for incompressible data
                 if (!oracle_mode && isNeedHelper) {
                     s_DoubleRankAccess->addData(1);

                     if (newTxn->getHelper() == NULL) {
                         c_Transaction *helper_txn = new c_Transaction(newTxn->getSeqNum(),
                                                                       newTxn->getTransactionMnemonic(),
                                                                       newTxn->getAddress(), newTxn->getDataWidth());

                         helper_txn->setChipAccessRatio(50);

                         c_HashedAddress l_hashedAddress = newTxn->getHashedAddress();

                         int new_rank = 0;
                         if (l_hashedAddress.getPChannel() == 0)
                             new_rank = 1;
                         else
                             new_rank = 0;

                         l_hashedAddress.setPChannel(new_rank);


                         unsigned l_bankId =
                                 l_hashedAddress.getBank()
                                 + l_hashedAddress.getBankGroup() * Banks
                                 + l_hashedAddress.getRank() * Banks * BGs
                                 + l_hashedAddress.getPChannel() * Banks * BGs * Ranks
                                 + l_hashedAddress.getChannel() * (PChs+1) * Banks * BGs * (Ranks );
                         //std::cout <<"row: "<<row<<" subrank: "<<subrank<<" bankid[old]: "<<l_hashedAddress.getBankId()<< " bankid[new]: "<<l_bankId<<std::endl;
                         unsigned l_rankId =
                                 +l_hashedAddress.getRank()
                                 + l_hashedAddress.getPChannel() * Ranks
                                 + l_hashedAddress.getChannel() * (PChs+1) * (Ranks );



                         l_hashedAddress.setRankId(l_rankId);
                         l_hashedAddress.setBankId(l_bankId);

                         helper_txn->setHashedAddress(l_hashedAddress);
                         helper_txn->setHelperFlag(true);
                         newTxn->setHelper(helper_txn);
                         newTxn->donotRespond();
                         m_ResQ.push_back(helper_txn);
                         newTxn->print(output, "newtxn\n", 0);
                         helper_txn->print(output, "helper\n", 0);
                     }
                 } else
                     s_SingleRankAccess->addData(1);
             }

             if (m_MReqQ.size() > 0) {
                 for (std::deque<c_Transaction *>::iterator l_mreq = m_MReqQ.begin(); l_mreq != m_MReqQ.end();) {
                     if (m_txnScheduler->push(*l_mreq)) {
                         l_mreq = m_MReqQ.erase(l_mreq);
                     } else
                         break;
                 }
             }
         }
        //insert new transaction into a transaction queue
        if(m_txnScheduler->push(newTxn)) {
            //With fast write response mode, controller sends a response for a write request as soon as it push the request to a transaction queue.
            if(k_enableQuickResponse && newTxn->isWrite())
            {
                //create a response and push it to the response queue.
                c_Transaction* l_txnRes = new c_Transaction(newTxn->getSeqNum(),newTxn->getTransactionMnemonic(),newTxn->getAddress(),newTxn->getDataWidth());
                l_txnRes->setResponseReady();
                sendResponse(l_txnRes);
                //m_ResQ.push_back(l_txnRes);
            }

            newTxn->m_time_inserted_TxnQ=m_simCycle;
            l_it = m_ReqQ.erase(l_it);


#ifdef __SST_DEBUG_OUTPUT__
            newTxn->print(output,"[Controller queues new txn]",m_simCycle);
#endif
        }
        else
            break;
    }

    // 3. run transaction Scheduler
    m_txnScheduler->run();

    // 4. run transaction converter
    m_txnConverter->run();

    // 5, run command scheduler
    m_cmdScheduler->run();

    // 6. run device driver
    m_deviceDriver->run();


    return false;
}



c_ControllerPCA::~c_ControllerPCA(){

}




//void c_ControllerPCA::handleContentEvent(SST::Event *ev)
void c_ControllerPCA::storeContent()
{
    for(auto &link: m_laneLinks)
    {
        SST::Event* ev = 0;
        while(ev=link->recv())
        {
            SST::MemHierarchy::MemEvent* req=dynamic_cast<SST::MemHierarchy::MemEvent*>(ev);
            uint64_t req_addr=req->getAddr();
            if(req->getCmd()==MemHierarchy::Command::Put) {
                uint64_t cacheline_addr = (req_addr >> 6) << 6;

                std::vector<uint8_t> compRatio_vector=(std::vector<uint8_t>)req->getPayload();
                compRatio_bdi[cacheline_addr] = compRatio_vector[0];
                //printf("cacheline addr:%llx comp_ratio:%d\n",cacheline_addr,compRatio_vector[0]);

            } else
            {
                fprintf(stderr,"[c_ControllerPCA] cpu command error!\n");
                exit(1);
            }

            delete req;
        }
    }
}

c_2LvPredictor::c_2LvPredictor(uint64_t robr_entries, int lipr_entries, int _num_col_per_lipr_entry, bool isSelectiveReplace_,Output* output)
{
    m_rowtable.resize(robr_entries);
    for(int j=0; j<robr_entries;j++)
        m_rowtable[j]=0;

    m_num_cache_entries=lipr_entries;
    m_num_col_per_cache_entry=_num_col_per_lipr_entry;

    m_cache_data.resize(m_num_cache_entries);
    m_cache_tag.resize(m_num_cache_entries);

    for(int i=0;i<m_num_cache_entries;i++) {
        m_cache_tag[i]=0;
        for (int j=0; j < m_num_col_per_cache_entry; j++) {
        std::pair<uint8_t, uint64_t> l_par = make_pair(0, 0);
        m_cache_data[i].push_back(l_par);
        }
    }

    cache_index_mask = m_num_cache_entries-1;
    cache_tag_offset=(int)log2(m_num_cache_entries);
    row_table_offset=(int)log2(robr_entries);
    m_row_table_mask = robr_entries-1;
    isSelectiveReplace=isSelectiveReplace_;
    m_output=output;
    m_hit_cnt=0;
    m_miss_cnt=0;
    m_predictor_lipr_hit=0;
    m_predictor_lipr_fail=0;
    m_predictor_lipr_miss=0;
    m_predictor_lipr_success=0;
    m_predictor_ropr_fail=0;
    m_predictor_ropr_success=0;
    //  cache_tag_offset=(int)log2(_num_col_per_cache_entry);
}
/*
c_2LvPredictor::c_2LvPredictor(uint64_t global_predictor_entries,uint64_t robr_entries, int lipr_entries,int pageSize,Output* output)
{
    m_global_predictor = new SCache(global_predictor_entries,1,0,pageSize);
    m_row_predictor = new SCache(robr_entries,16,0,pageSize);
    m_line_predictor = new SCache(lipr_entries,16,0.pageSize);
    m_output=output;
    m_hit_cnt=0;
    m_miss_cnt=0;
    m_predictor_lipr_hit=0;
    m_predictor_lipr_fail=0;
    m_predictor_lipr_miss=0;
    m_predictor_lipr_success=0;
    m_predictor_ropr_fail=0;
    m_predictor_ropr_success=0;
    m_row_predictor= nullptr;
    m_line_predictor=nullptr;
}

int c_2LvPredictor::update_v2(int col, int row, int compSize) {

int c_2LvPredictor::getPredictedSize_v2(uint64_t addr, int col, uint32_t actual_size)
{
    int predict_size_linelv=-1;
    int predict_size_rowlv=-1;

    if(m_row_predictor)
    {
        if(m_row_predictor->isHit(addr,false))
        {
            uint64_t data = m_row_predictor->getData(addr);
            if(data==3)
                predict_size_rowlv=50;
            else
                predict_size_rowlv=100;

            //update state
            if(data<3) {
                data++;
                m_row_predictor->setData(addr,data);
            }

        }
        else {
            SST::CACHE::MCache_Entry victim = m_row_predictor->install(addr,false);
            uint64_t victim_addr        =     m_row_predictor->getAddrFromTag(victim.tag);
            m_row_predictor->install(addr,false);
            m_row_predictor->setData(addr,0);
            m_row_predictor->getData()
            predict_size_rowlv          =     100;

        }
    }

    //1. see line_level_predictor
    if(m_line_predictor) {

        if(m_line_predictor->isHit(addr,false)) {
            uint64_t data = m_line_predictor->getData(addr);
            if (((data >> col) & 1) == 1)
                predict_size_line = 50;
            else
                predict_size = 100;
        }
        else
        {
            m_line_predictor->setData()
            predict_size = 100;
        }
    }

        //statistics
        if(predict_size<0) {
            m_predictor_lipr_miss++;
            m_output->verbose(CALL_INFO,1,0,"LiPR miss, col:%d, row_:%d\n",col,row_);
            //   printf("LiPR miss, misscount:%lld, col:%d, row_:%d\n",m_predictor_lipr_miss,col,row_);
        }
        else
        {
            m_predictor_lipr_hit++;
            if(actual_size<=50 && predict_size<=50 || actual_size>50 && predict_size>50) {
                m_predictor_lipr_success++;
                m_output->verbose(CALL_INFO,1,0,"LiPR Hit prediction success, actual_size:%d predict_size:%d, col:%d, row_:%d, \n",actual_size, predict_size,col,row_);
                //    printf("LiPR Hit prediction success, success:%lld, actual_size:%d predict_size:%d, col:%d, row_:%d, \n",m_predictor_lipr_success, actual_size, predict_size,col,row_);
            }
            else {
                m_predictor_lipr_fail++;
                m_output->verbose(CALL_INFO,1,0,"LiPR Hit prediction miss, actual_size:%d predict_size:%d, col:%d, row_:%d, \n",actual_size, predict_size,col,row_);
                //    printf("LiPR Hit prediction miss, fail:%lld, actual_size:%d predict_size:%d, col:%d, row_:%d, \n",m_predictor_lipr_fail, actual_size, predict_size,col,row_);
            }
        }
    }

    //2. see rowtable
    if(predict_size<0) {
        int row_idx=row_ & m_row_table_mask;
        int predict_bit = m_rowtable[row_idx];
        if (predict_bit == 3)
            predict_size = 50;
        else
            predict_size = 100;

        if(actual_size<=50 && predict_size<=50 || actual_size>50 && predict_size>50) {
            m_predictor_ropr_success++;
            m_output->verbose(CALL_INFO,1,0,"RoPR prediction success, actual_size:%d predict_size:%d\n",actual_size, predict_size,col,row_);
            //    printf("RoPR prediction success, success:%lld, actual_size:%d predict_size:%d\n",m_predictor_ropr_success,actual_size, predict_size,col,row_);
        }
        else {
            m_predictor_ropr_fail++;
            m_output->verbose(CALL_INFO,1,0,"RoPR prediction fail, actual_size:%d predict_size:%d\n",actual_size, predict_size,col,row_);
            //    printf("RoPR prediction fail, fail:%lld, actual_size:%d predict_size:%d\n",m_predictor_ropr_fail,actual_size, predict_size,col,row_);
        }
    }

    return predict_size;
}


uint8_t c_2LvPredictor::updateRowTable_v2(int cacheline_size, uint64_t addr)
{


    uint8_t prev_state=m_rowtable.at(row_idx);
    uint8_t new_state=0;
    if(cacheline_size<=50)
        new_state=prev_state+1;
    else
        new_state=0;

    if(new_state>3)
        new_state=3;

    m_rowtable.at(row_idx)=new_state;
    m_output->verbose(CALL_INFO,1,0,"update row table, row:%lld, row_idx:%lld, newstate:%d\n",row_,row_idx,new_state);
    //printf("update row table, row:%lld, row_idx:%lld, newstate:%d\n",row_,row_idx,new_state);
    return new_state;
}

*/

uint8_t c_2LvPredictor::updateRowTable(int cacheline_size, int row_)
{
    int row_idx = row_ & m_row_table_mask;
    uint8_t prev_state=m_rowtable.at(row_idx);
    uint8_t new_state=0;
    if(cacheline_size<=50)
        new_state=prev_state+1;
    else
        new_state=0;

    if(new_state>3)
        new_state=3;

    m_rowtable.at(row_idx)=new_state;
    m_output->verbose(CALL_INFO,1,0,"update row table, row:%lld, row_idx:%lld, newstate:%d\n",row_,row_idx,new_state);
    //printf("update row table, row:%lld, row_idx:%lld, newstate:%d\n",row_,row_idx,new_state);
    return new_state;
}

int c_2LvPredictor::getPredictedSize(uint32_t col, uint32_t row_, uint32_t actual_size)
{
    int predict_size=-1;

    //1. see metacache
    if(m_num_cache_entries>0) {
        predict_size = getCompSizeFromCache(col, row_);

        //statistics
        if(predict_size<0) {
            m_predictor_lipr_miss++;
            m_output->verbose(CALL_INFO,1,0,"LiPR miss, col:%d, row_:%d\n",col,row_);
         //   printf("LiPR miss, misscount:%lld, col:%d, row_:%d\n",m_predictor_lipr_miss,col,row_);
        }
        else
        {
            m_predictor_lipr_hit++;
            if(actual_size<=50 && predict_size<=50 || actual_size>50 && predict_size>50) {
                m_predictor_lipr_success++;
                m_output->verbose(CALL_INFO,1,0,"LiPR Hit prediction success, actual_size:%d predict_size:%d, col:%d, row_:%d, \n",actual_size, predict_size,col,row_);
            //    printf("LiPR Hit prediction success, success:%lld, actual_size:%d predict_size:%d, col:%d, row_:%d, \n",m_predictor_lipr_success, actual_size, predict_size,col,row_);
            }
            else {
                m_predictor_lipr_fail++;
                m_output->verbose(CALL_INFO,1,0,"LiPR Hit prediction miss, actual_size:%d predict_size:%d, col:%d, row_:%d, \n",actual_size, predict_size,col,row_);
            //    printf("LiPR Hit prediction miss, fail:%lld, actual_size:%d predict_size:%d, col:%d, row_:%d, \n",m_predictor_lipr_fail, actual_size, predict_size,col,row_);
            }
        }
    }

    //2. see rowtable
    if(predict_size<0) {
        int row_idx=row_ & m_row_table_mask;
        int predict_bit = m_rowtable[row_idx];
        if (predict_bit == 3)
            predict_size = 50;
        else
            predict_size = 100;

        if(actual_size<=50 && predict_size<=50 || actual_size>50 && predict_size>50) {
            m_predictor_ropr_success++;
            m_output->verbose(CALL_INFO,1,0,"RoPR prediction success, actual_size:%d predict_size:%d\n",actual_size, predict_size,col,row_);
        //    printf("RoPR prediction success, success:%lld, actual_size:%d predict_size:%d\n",m_predictor_ropr_success,actual_size, predict_size,col,row_);
        }
        else {
            m_predictor_ropr_fail++;
            m_output->verbose(CALL_INFO,1,0,"RoPR prediction fail, actual_size:%d predict_size:%d\n",actual_size, predict_size,col,row_);
        //    printf("RoPR prediction fail, fail:%lld, actual_size:%d predict_size:%d\n",m_predictor_ropr_fail,actual_size, predict_size,col,row_);
        }
    }

    return predict_size;
}


int c_2LvPredictor::getCompSizeFromCache(int col, int row)
{
    int compSize=-1;

/*    if(isHighAssocRowTable)
    {
        uint64_t addr = row<<1 + (col/64);
        if(m_highAssocRowTable.isHit(addr))
        {
            uint64_t data = 
        }

    }
    else */{
        int index = row & cache_index_mask;
        int tag = row >> cache_tag_offset;
        int col_index = (int) ((double) col / ((double) 128 /
                                               (double) m_num_col_per_cache_entry));  //assume that the number of dram column is 128
        int col_tag = col >> (int) log2(m_num_col_per_cache_entry);

        if (m_cache_tag.at(index) == tag) {
            std::pair<uint8_t, uint64_t> comp_data = m_cache_data[index][col_index];

            if (comp_data.first == col_tag) {
                compSize = comp_data.second;
                m_cl_hit_cnt++;
            } else {
                m_cl_miss_cnt++;
                compSize = comp_data.second;
            }

            m_hit_cnt++;
            m_output->verbose(CALL_INFO, 1, 0,
                              "[Hit] col: %d, row:%d index: %d tag: %d, col_index: %d, col_tag:%d compSize:%d\n", col,
                              row, index, tag, col_index, col_tag, compSize);
            //    printf("[Hit] hitcount:%lld, col: %d, row:%d index: %d tag: %d, col_index: %d, col_tag:%d compSize:%d\n",this->m_predictor_lipr_hit, col, row, index, tag, col_index, col_tag, compSize);
        } else {
            m_miss_cnt++;
            m_cl_miss_cnt++;
            compSize = -1;
            m_output->verbose(CALL_INFO, 1, 0,
                              "[Miss] col: %d, row:%d index: %d tag: %d, col_index: %d, col_tag:%d compSize:%d\n", col,
                              row, index, tag, col_index, col_tag, compSize);
            //   printf("[Miss] misscount:%lld, col: %d, row:%d index: %d tag: %d, col_index: %d, col_tag:%d compSize:%d\n",m_predictor_lipr_miss, col, row, index, tag, col_index, col_tag, compSize);
        }
    }
    return compSize;
}

int c_2LvPredictor::update(int col, int row, int compSize) {
    int index = row & cache_index_mask;
    int tag = row >> cache_tag_offset;
    int col_index = (int) ((double) col / ((double) 128 /
                                           (double) m_num_col_per_cache_entry));  //assume that the number of dram column is 128
    int col_tag = col >> (int) log2(m_num_col_per_cache_entry);

    uint8_t next_state = updateRowTable(compSize, row);

    //cache miss and the row state is not highly compressible
    bool doReplace=true;
    if(isSelectiveReplace)
        if(next_state!=3)
            doReplace=false;


    if (doReplace && m_cache_tag.at(index) != tag) {
        m_cache_tag.at(index) = tag;

        int fill_comp_size = compSize;

        for (int i = 0; i < m_num_col_per_cache_entry; i++) {
            std::pair<uint8_t, uint64_t> comp_data = make_pair(col_tag, fill_comp_size);
            m_cache_data[index][i] = comp_data;
        }

        std::pair<uint8_t, uint64_t> comp_data = make_pair(col_tag, compSize);
        m_cache_data[index][col_index] = comp_data;
        // }
    }//cache hit
    else {
        std::pair<uint8_t, uint64_t> comp_data = make_pair(col_tag, compSize);
        m_cache_data[index][col_index] = comp_data;
    }
}


//////////////////////////////////////////////////////////////////
c_2LvPredictor_new::c_2LvPredictor_new(uint64_t global_predictor_entries, uint64_t ropr_entries, int ropr_cols, int pageSize, int  g_predictor_threshold, Output* output) {
    m_row_predictor = new SCache(ropr_entries, 16, 0, pageSize);
    m_row_predictor_data.clear();

    m_pageSize=pageSize;
    m_page_offset_size = log2(pageSize);
    m_ropr_col_offset_size = log2(ropr_cols);
    m_ropr_col_num=ropr_cols;

    if(global_predictor_entries>0)
        m_global_predictor.resize(global_predictor_entries);

    m_global_predictor_mask=global_predictor_entries;
    m_global_prediction_threadhold=g_predictor_threshold;
    for(auto &it:m_global_predictor) {
        it = 0;
    }
}

int c_2LvPredictor_new::getPredictedSize(uint64_t addr, uint32_t actual_size)
{

    int predict_size=-1;
    uint8_t global_prediction = 0;
    int global_predictor_index = 0;
    int global_predictor_data = 0;
    uint64_t page_addr = addr >> m_page_offset_size;
    int col_num=(m_pageSize/64);

    int col = (addr >> 6) % col_num;
    int col_idx = (int) ((double) col / ((double) col_num /
                                           (double) m_ropr_col_num));  //assume that the number of dram column is 128

    if(m_global_predictor.size()>0) {
        global_predictor_index=(addr>>m_page_offset_size)%m_global_predictor_mask;
        global_predictor_data = m_global_predictor[global_predictor_index];
    }



    //get global prediction
    if(global_predictor_data>m_global_prediction_threadhold)
        global_prediction=3;
    else
        global_prediction=0;

    //statistic for global predictor
    if((global_prediction==3 && actual_size<=50 )|| (global_prediction!=3 && actual_size>50))
        m_predictor_global_success++;
    else
        m_predictor_global_fail++;

    //update global predictor
    if(m_global_predictor.size()>0) {
        if (actual_size <= 50 && m_global_predictor[global_predictor_index] < 255)
            m_global_predictor[global_predictor_index]++;
        else if (actual_size > 50 && m_global_predictor[global_predictor_index] > 0)
            m_global_predictor[global_predictor_index]--;
    }

    //row_comp_predictor
    bool ishit=false;
    if(m_row_predictor && predict_size<0) {
        ishit=m_row_predictor->isHit(addr, true);
        if (ishit) {

            if (m_row_predictor_data.find(page_addr) == m_row_predictor_data.end()) {
                printf("no data in row predictor, addr:%llx page_addr:%llx\n",addr, page_addr);
                exit(1);
            }


            uint8_t data = m_row_predictor_data[page_addr].at(col_idx);
            if (data == 3)
                predict_size = 50;
            else
                predict_size = 100;

            //update state
            if (data < 3) {
                data++;
                m_row_predictor_data[page_addr].at(col_idx) = data;
            }
        } else {
            CACHE::MCache_Entry victim = m_row_predictor->install(addr, true);
            uint64_t victim_page_addr = victim.tag;

            //clean victim
            if (victim.valid && (m_row_predictor_data.find(victim_page_addr) != m_row_predictor_data.end()))
                m_row_predictor_data.erase(victim_page_addr);

            //set new page
            std::vector<uint8_t> data_tmp;
            for(int i=0; i<m_ropr_col_num;i++) {
                data_tmp.push_back(global_prediction);
            }
            m_row_predictor_data[page_addr]=data_tmp;

            //default prediction on predictor miss
            if (global_prediction == 3)
                predict_size = 50;
            else
                predict_size = 100;
        }


        //update state
 //       printf("before:%lld\n",m_row_predictor_data[page_addr]);
        if (actual_size<=50 && m_row_predictor_data[page_addr].at(col_idx)  < 3)
            m_row_predictor_data[page_addr].at(col_idx)++;
        else if(actual_size>50 && m_row_predictor_data[page_addr].at(col_idx)>0)
            m_row_predictor_data[page_addr].at(col_idx)--;
   //     printf("after:%lld\n",m_row_predictor_data[page_addr]);
    }
/*
    if(m_predictor_ropr_success%1000==0)
        printf("addr:%llx global_index:%d m_global_data:%lld m_row_predictor_data:%d actual_size:%d predic_size:%d success:%lld fail%lld success?:%d\n",
            addr,global_predictor_index,global_predictor_data,m_row_predictor_data[page_addr],actual_size,predict_size,
                m_predictor_ropr_success,m_predictor_ropr_fail, (actual_size<=50 && predict_size<=50 || actual_size>50 && predict_size>50)?1:0);
  */
    //statistics
        if(!ishit) {
            m_predictor_ropr_miss++;
        }
        else
        {
            m_predictor_ropr_hit++;
        }
       if(actual_size<=50 && predict_size<=50 || actual_size>50 && predict_size>50) {
            m_predictor_ropr_success++;
        }
        else {
            m_predictor_ropr_fail++;
        }
    return predict_size;
}


void c_ControllerPCA::handleInDeviceResPtrEvent(SST::Event *ev){
    c_CmdResEvent* l_cmdResEventPtr = dynamic_cast<c_CmdResEvent*>(ev);
    if (l_cmdResEventPtr) {
        uint64_t l_resSeqNum = l_cmdResEventPtr->m_payload->getSeqNum();
        // need to find which txn matches the command seq number in the txnResQ
        c_Transaction* l_txnRes = nullptr;
        std::deque<c_Transaction*>::iterator l_txIter;

        for(l_txIter=m_ResQ.begin() ; l_txIter!=m_ResQ.end();l_txIter++) {
            if((*l_txIter)->matchesCmdSeqNum(l_resSeqNum)) {
                l_txnRes = *l_txIter;
                break;
            }
        }

        if(l_txnRes == nullptr) {
            std::cout << "Error! Couldn't find transaction to match cmdSeqnum " << l_resSeqNum << std::endl;
            std::cout << "meta data txn?" << l_cmdResEventPtr->m_payload->isMetadataCmd() << std::endl;
            std::cout << " helper?" << l_cmdResEventPtr->m_payload->isHelper() << std::endl;
            l_cmdResEventPtr->m_payload->print(m_simCycle);
            l_cmdResEventPtr->m_payload->getTransaction()->print();

            exit(-1);
        }
        else
        {
            output->verbose(CALL_INFO,1,0, "Cycle: %lld, txn is found, txnNum:%lld cmdSeq:%lld, addr:%lld, helper:%d\n",m_simCycle,l_txnRes->getSeqNum(),l_txnRes->getAddress(), l_txnRes->isHelper());
        }

        const unsigned l_cmdsLeft = l_txnRes->getWaitingCommands() - 1;
        l_txnRes->setWaitingCommands(l_cmdsLeft);
        if (l_cmdsLeft == 0) {
            l_txnRes->setResponseReady();

            // With quick response mode, controller sends a response to a requester for a write request as soon as the request is pushed to a transaction queue
            // So, we don't need to send another response at this time. Just erase the request in the response queue.
            if ( k_enableQuickResponse && l_txnRes->isWrite()) {
                m_ResQ.erase(l_txIter);
                delete l_txnRes;
            }
            else {
                if (!l_txnRes->isResponseRequired()) {
                    m_ResQ.erase(l_txIter);
                    delete l_txnRes;
                } else {
                    sendResponse(l_txnRes);
                    m_ResQ.erase(l_txIter);
                }
            }
        }

        delete l_cmdResEventPtr->m_payload;         //now, free the memory space allocated to the commands for a transaction
        delete l_cmdResEventPtr;

    } else {
        std::cout << __PRETTY_FUNCTION__ << "ERROR:: Bad event type!"
                  << std::endl;
    }
}
