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

using namespace SST;
using namespace SST::n_Bank;

c_ControllerPCA::c_ControllerPCA(ComponentId_t id, Params &x_params) :
        c_Controller(id, x_params){


    bool l_found;
    // Set up backing store if needed
    m_backing_size = x_params.find<uint64_t>("backing_size",10000,l_found);
    // internal params
    verbosity = x_params.find<int>("verbose", 0);

    if(!l_found)
    {
        fprintf(stderr,"[c_ControllerPCA] backing store size is missing!! exit\n");
        exit(1);
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


    pca_mode=x_params.find<bool>("pca_enable",false);
    oracle_mode=x_params.find<bool>("oracle_mode",false);

    m_compEngine = new c_CompressEngine(verbosity,true);


    metadata_predictor= x_params.find<int>("metadata_predictor",0);
    if(metadata_predictor==1)       //memzip
    {
        bool found=false;
        uint32_t metacache_entry_num = x_params.find<uint32_t>("metaCache_entries",0,found); //128KB, 130b per entry
        if(!found)
        {
            fprintf(stderr,"metaCache_entries is missing exit!!");
            exit(-1);
        }

        uint32_t rowsize=m_deviceDriver->getNumColPerBank()*64;
        double hitrate= x_params.find<double>("metacache_hitrate",0,found);
        metacache = new c_MetaCache(rowsize,metacache_entry_num,output,hitrate);
    }
    else if(metadata_predictor==2)
    {
        bool found=false;

        uint32_t metacache_entry_num = x_params.find<uint32_t>("metaCache_entries",16*1024,found); //128KB, 80b per entry
        int metacache_entry_colnum = x_params.find<uint32_t>("metaCache_Colnum",16,found); //128KB, 130b per entry
        cmpSize_predictor = new c_2LvPredictor(m_rownum*m_total_num_banks,metacache_entry_num,metacache_entry_colnum,output);
    }

    /*---- CONFIGURE LINKS ----*/

    m_contentLink = configureLink( "contentLink",new Event::Handler<c_ControllerPCA>(this,&c_ControllerPCA::handleContentEvent) );


   s_CompRatio = registerStatistic<double>("compRatio");
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


}

void c_ControllerPCA::init(unsigned int phase){
    if(phase==0)
       if(m_contentLink)
            m_contentLink->sendInitData(new MemHierarchy::MemEventInit(this->getName(),MemHierarchy::MemEventInit::InitCommand::Region));
}


void c_ControllerPCA::finish() {
    uint64_t cnt=0;
    uint64_t normalized_size_sum=0;
    uint64_t compressed_size_50=0;
    uint64_t compressed_size_100=0;
    for(int i=0;i<50;i++)
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
    if(metacache&&metadata_predictor==1) {
        s_MemzipMetaCacheHit->addData(metacache->getHitCnt());
        s_MemzipMetaCacheMiss->addData(metacache->getMissCnt());
        printf("memzip metacache hit: %lld\n",metacache->getHitCnt());
        printf("memzip metacache miss: %lld\n",metacache->getMissCnt());
    }
    else if(cmpSize_predictor&&metadata_predictor==2)
    {
        printf("predictor metacache row hit: %lld\n",cmpSize_predictor->getHitCnt());
        printf("predictor metacache row miss: %lld\n",cmpSize_predictor->getMissCnt());
        printf("predictor metacache cacheline hit: %lld\n",cmpSize_predictor->getClHitCnt());
        printf("predictor metacache cacheline miss: %lld\n",cmpSize_predictor->getClMissCnt());
        printf("predictor prediction success: %lld\n",cmpSize_predictor->getPredSuccessCnt());
        printf("predictor prediction fail: %lld\n",cmpSize_predictor->getPredFailCnt());
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

    m_simCycle++;

    sendResponse();

    // 0. update device driver
    m_deviceDriver->update();

    // 1. Convert physical address to device address
    // 2. Push transactions to the transaction queue
    for(std::deque<c_Transaction*>::iterator l_it=m_ReqQ.begin() ; l_it!=m_ReqQ.end();)
    {
        c_Transaction* newTxn= *l_it;

        if(newTxn->hasHashedAddress()== false)
        {
            c_HashedAddress l_hashedAddress;
            m_addrHasher->fillHashedAddress(&l_hashedAddress, newTxn->getAddress());
            newTxn->setHashedAddress(l_hashedAddress);
        }

        //If new transaction hits in the transaction queue, send a response immediately and do not access memory
        if(k_enableQuickResponse && m_txnScheduler->isHit(newTxn))
        {
            newTxn->setResponseReady();
            //delete the new transaction from request queue
            l_it=m_ReqQ.erase(l_it);

#ifdef __SST_DEBUG_OUTPUT__
            newTxn->print(output,"[TxnQueue hit]",m_simCycle);
#endif
            continue;
        }

        uint64_t addr = (newTxn->getAddress() >> 6) << 6;
        //calculate the compressed size of cacheline
        if(compression_en&&newTxn->getCompressedSize()<0) {

                if(backing_.find(addr)==backing_.end()) {
                   // std::cout << "Error!! cacheline is not found\n";
                    backing_[addr] = 0;
                    s_BackingMiss->addData(1);
                }

                uint8_t normalized_size = backing_[addr];
                m_normalized_size[normalized_size]++;

                newTxn->setCompressedSize(normalized_size);

                int bankid = newTxn->getHashedAddress().getBankId();
                uint32_t row = newTxn->getHashedAddress().getRow();
                int col = newTxn->getHashedAddress().getCol();

                //record statistics
                if(m_row_stat.find(bankid)==m_row_stat.end()) {
                    m_row_stat[bankid][row]= new c_RowStat(m_colnum);
                }
                else if(m_row_stat[bankid].find(row)==m_row_stat[bankid].end())
                {
                    m_row_stat[bankid][row] = new c_RowStat(m_colnum);
                }

                m_row_stat[bankid].at(row)->record(normalized_size, col);

        }

        if(loopback_en==true) {
            newTxn->setResponseReady();
            l_it=m_ReqQ.erase(l_it);
            continue;
        }

        //partial-chip access
         if(pca_mode)
         {
             assert(m_deviceDriver->getNumRanksPerChannel()==1);

            //determine subrank
             uint32_t row = newTxn->getHashedAddress().getRow();
             unsigned subrank=row&0x1;
             int Chs=m_deviceDriver->getNumChannel();
             int Ranks=m_deviceDriver->getNumRanksPerChannel();
             int BGs=m_deviceDriver->getNumBankGroupsPerRank();
             int Banks=m_deviceDriver->getNumBanksPerBankGroup();

             c_HashedAddress l_hashedAddress=newTxn->getHashedAddress();
             l_hashedAddress.setRank(subrank);

             unsigned l_bankId =
                     l_hashedAddress.getBank()
                     + l_hashedAddress.getBankGroup() * Banks
                     + l_hashedAddress.getRank()    * Banks * BGs
                     + l_hashedAddress.getChannel()   * 1 * Banks*BGs  * (Ranks+1);
            //std::cout <<"row: "<<row<<" subrank: "<<subrank<<" bankid[old]: "<<l_hashedAddress.getBankId()<< " bankid[new]: "<<l_bankId<<std::endl;
             unsigned l_rankId =
                     + l_hashedAddress.getRank()
                     + l_hashedAddress.getChannel()   * 1 * (Ranks+1);

                l_hashedAddress.setRankId(l_rankId);
                l_hashedAddress.setBankId(l_bankId);
                newTxn->setHashedAddress(l_hashedAddress);


             bool isNeedHelper=false;


             if(!newTxn->isMetaDataSkip())
             {
                 newTxn->setMetaDataSkip();
                 switch(metadata_predictor) {
                     case 0: //perfect predictor
                     {
                         isNeedHelper = newTxn->needHelper();
                         newTxn->setChipAccessRatio(newTxn->getCompressedSize());
                         break;
                     }
                     case 1:  //memzip metacache
                     {
                         assert(metacache != NULL);

                         if (metacache->isHit(addr)) {
                             isNeedHelper = newTxn->needHelper();
                             newTxn->setChipAccessRatio(newTxn->getCompressedSize());
                         } else {
                             isNeedHelper = true;

                             //write back metadata
                             uint64_t victim_row_addr = metacache->fill(addr);
                             uint64_t writeback_addr = metacache->getMetaDataAddress(victim_row_addr);

                             // victim_row_addr ^(victim_row_addr >> 32);
                            c_Transaction *writebackTxn
                                     = new c_Transaction(newTxn->getSeqNum(), e_TransactionType::WRITE, writeback_addr,
                                                         1);

                             c_HashedAddress l_hashedAddress2;
                             m_addrHasher->fillHashedAddress(&l_hashedAddress2, writeback_addr);

                             writebackTxn->setHashedAddress(l_hashedAddress2);
                             writebackTxn->setMetaDataSkip();
                             //writebackTxn->setHelperFlag(true);
                             writebackTxn->donotRespond();

                             writebackTxn->setCompressedSize(50);
                             writebackTxn->setChipAccessRatio(50);

                             m_MReqQ.push_back(writebackTxn);
                             m_ResQ.push_back(writebackTxn);


                             //fetch metadata
                             uint64_t fetch_addr = metacache->getMetaDataAddress(addr);
                             c_Transaction *fillTxn
                                    = new c_Transaction((ulong) newTxn->getSeqNum(), e_TransactionType::READ, fetch_addr, 1);

                             c_HashedAddress l_hashedAddress3;
                             m_addrHasher->fillHashedAddress(&l_hashedAddress3, fetch_addr);

                             fillTxn->setMetaDataSkip();
                             fillTxn->setHashedAddress(l_hashedAddress3);
                             //fillTxn->setHelperFlag(true);
                             fillTxn->donotRespond();
                             fillTxn->setCompressedSize(50);
                             fillTxn->setChipAccessRatio(50);

                             m_MReqQ.push_back(fillTxn);
                             m_ResQ.push_back(fillTxn);

                         }
                         break;
                     }
                     case 2:{
                         assert(cmpSize_predictor!=NULL);
                         int col=newTxn->getHashedAddress().getCol();
                         int row=newTxn->getHashedAddress().getRow();
                         int bankid=newTxn->getHashedAddress().getBankId();
                         int rowoffset=(int)log2(m_rownum);
                         int rowid=(bankid << rowoffset) | row;
                         bool isWrite = newTxn->isWrite();

                         int compSize=100;

                         if(isWrite) {
                             compSize=newTxn->getCompressedSize();
                             isNeedHelper = newTxn->needHelper();
                         }
                         else{
                             int predSize=cmpSize_predictor->getPredictedSize(col,rowid,newTxn->getCompressedSize());

                             if(predSize > 50)
                                 isNeedHelper=true;
                             else
                                 isNeedHelper=false;

                             compSize=newTxn->getCompressedSize();
                         }

                         cmpSize_predictor->update(col, rowid,newTxn->getCompressedSize());

                         newTxn->setChipAccessRatio(compSize);
                         break;
                     }
                     default: {
                         isNeedHelper = true;
                         newTxn->setChipAccessRatio(50);
                         break;
                     }
                 }
             }


            //add helper transaction for incompressible data
            if(!oracle_mode&&isNeedHelper)
            {
                s_DoubleRankAccess->addData(1);

                if(newTxn->getHelper()==NULL) {
                    c_Transaction *helper_txn = new c_Transaction(newTxn->getSeqNum(), newTxn->getTransactionMnemonic(),
                                                                  newTxn->getAddress(), newTxn->getDataWidth());

                    helper_txn->setChipAccessRatio(50);

                    c_HashedAddress l_hashedAddress=newTxn->getHashedAddress();

                    int new_rank = 0;
                    if (l_hashedAddress.getRank() == 0)
                        new_rank = 1;
                    else
                        new_rank = 0;

                    l_hashedAddress.setRank(new_rank);

                    unsigned l_bankId =
                     l_hashedAddress.getBank()
                     + l_hashedAddress.getBankGroup() * Banks
                     + l_hashedAddress.getRank()    * Banks * BGs
                     + l_hashedAddress.getChannel()   * 1 * Banks*BGs  * (Ranks+1);
            //std::cout <<"row: "<<row<<" subrank: "<<subrank<<" bankid[old]: "<<l_hashedAddress.getBankId()<< " bankid[new]: "<<l_bankId<<std::endl;

                    unsigned l_rankId =
                     + l_hashedAddress.getRank()
                     + l_hashedAddress.getChannel()   * 1 * (Ranks+1);


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
            }
            else
                s_SingleRankAccess->addData(1);
         }

        //insert new transaction into a transaction queue
        if(m_txnScheduler->push(newTxn)) {
            //With fast write response mode, controller sends a response for a write request as soon as it push the request to a transaction queue.
            if(k_enableQuickResponse && newTxn->isWrite())
            {
                //create a response and push it to the response queue.
                c_Transaction* l_txnRes = new c_Transaction(newTxn->getSeqNum(),newTxn->getTransactionMnemonic(),newTxn->getAddress(),newTxn->getDataWidth());
                l_txnRes->setResponseReady();
                m_ResQ.push_back(l_txnRes);
            }


            l_it = m_ReqQ.erase(l_it);


#ifdef __SST_DEBUG_OUTPUT__
            newTxn->print(output,"[Controller queues new txn]",m_simCycle);
#endif
        }
        else
            break;
    }

    if(m_MReqQ.size()>0) {
        for (auto &metadata_req: m_MReqQ) {
            m_ReqQ.push_back(metadata_req);
        }
        m_MReqQ.clear();
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




void c_ControllerPCA::handleContentEvent(SST::Event *ev)
{
    SST::MemHierarchy::MemEvent* req=dynamic_cast<SST::MemHierarchy::MemEvent*>(ev);
    if(req->getCmd()!=MemHierarchy::Command::NULLCMD) {
        uint64_t cacheline_addr = (req->getAddr() >> 6) << 6;
        uint64_t *mem_ptr_64;

        //assert(cacheline_addr < m_backing_size);
        int size=req->getSize();
        c_Cacheline* new_cacheline=new c_Cacheline(req->getPayload());

        if(compression_en) {
            int compressed_size = m_compEngine->getCompressedSize(new_cacheline->getData(), COMP_ALG::BDI);
            int normalized_size = (int) ((double) compressed_size / (double) 512 * 100);

            backing_[cacheline_addr] = normalized_size;
        }

        if(verbosity>2) {
            uint64_t cacheline_vaddr = (req->getVirtualAddress() >> 6) << 6;
            uint32_t offset = 0;
            for (int j = 0; j < 8; j++) {
                mem_ptr_64 = (uint64_t *) new_cacheline->getData();
                output->verbose(CALL_INFO, 10, 0, "paddr: %llx vaddr: %llx data: %llx \n", cacheline_addr + j * 8,
                                cacheline_vaddr + j * 8, *mem_ptr_64);
            }
        }

        delete new_cacheline;

    }
    delete ev;
}