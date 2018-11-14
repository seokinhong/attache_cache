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
#include "c_Cache.hpp"
#include <bitset>
#include "Cache.hpp"
#include "c_TxnReqEvent.hpp"
#include "c_TxnResEvent.hpp"
#include "c_CmdResEvent.hpp"


using namespace std;
using namespace SST;
using namespace SST::n_Bank;
using namespace SST::MemHierarchy;
using namespace SST::CACHE;

c_Cache::c_Cache(ComponentId_t x_id, Params &params):Component(x_id) {

    bool l_found=false;

    //set verbose output
    int verbosity               =   params.find<int>("verbose", 0);
    output                      =   new SST::Output("CramSim.Cache[@f:@l:@p] ", verbosity, 0, SST::Output::STDOUT);

    //set our clock
    std::string l_clockFreqStr  =   (std::string)params.find<std::string>("ClockFreq", "1GHz", l_found);
    registerClock(l_clockFreqStr, new Clock::Handler<c_Cache>(this, &c_Cache::clockTic));


    // set cache structure
    uint64_t cache_size     =   params.find<uint64_t>("cache_size",4*1024*1024); //default:4MB
    uint32_t assoc          =   params.find<uint32_t>("associativity",16); // default 16way
    uint32_t cache_linesize =   params.find<uint32_t>("line_size",64); //default 64B
    string replacement      =   params.find<string>("repl_policy","LRU");
    m_cacheLatency          =   params.find<uint32_t>("latency",20);
    MCache_ReplPolicy_Enum repl                =   MCache_ReplPolicy_Enum::REPL_LRU;
    uint32_t sets                =   cache_size/(assoc*64);

    if(replacement=="LRU")
        repl = MCache_ReplPolicy_Enum ::REPL_LRU;
    else if(replacement=="SRRIP")
        repl = MCache_ReplPolicy_Enum ::REPL_SRRIP;
    else if(replacement=="DRRIP")
        repl = MCache_ReplPolicy_Enum ::REPL_DRRIP;
    else if(replacement=="DIP")
        repl = MCache_ReplPolicy_Enum ::REPL_DIP;
    else if(replacement=="RANDOM")
        repl = MCache_ReplPolicy_Enum ::REPL_RND;
    else
    {
        printf("unsupported replacement policy:5s\n",replacement.c_str());
        exit(-1);
    }

    m_cache = new SCache(sets,assoc,repl,cache_linesize);

    printf("[cramsim cache] cache_size(KB):%d\n", cache_size/1024);
    printf("[cramsim cache] repl_policy:%s\n",replacement.c_str());
    printf("[cramsim cache] latency:%d\n",m_cacheLatency);
    printf("[cramsim cache] line size:%d\n", cache_linesize);
    printf("[cramsim cache] enableAllHit? %d\n", enableAllHit==true?1:0);



    enableAllHit                =   (bool)params.find<bool>("enableAllHit",false);

    //---- configure link ----//
    m_linkMem   =   configureLink("memLink",new Event::Handler<c_Cache>(this,&c_Cache::handleMemEvent));
    m_linkCPU   =   configureLink("cpuLink",new Event::Handler<c_Cache>(this,&c_Cache::handleCpuEvent));

    // link for the content delivery
    int contentline_num= params.find<int>("contentline_num",0,l_found);
    if(l_found==false)
    {
        fprintf(stderr,"[C_Cache] contentline_num is miss\n");
    }


    for (int i = 0; i < contentline_num; i++) 
    {
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


    m_seqnum    =   0;
    m_simCycle  =   0;

    // initialize statistics
    s_accesses  =   registerStatistic<uint64_t>("accesses");
    s_hit       =   registerStatistic<uint64_t>("hits");
    s_miss      =   registerStatistic<uint64_t>("misses");
    s_readRecv  =   registerStatistic<uint64_t>("reads");
    s_writeRecv = registerStatistic<uint64_t>("writes");
    s_BackingMiss = registerStatistic<uint64_t>("backing_store_miss");

}

/*
 * Setup the link between components
 */
void c_Cache::init(unsigned int phase) {
        MemEventInit *ev = NULL;
        while(ev=dynamic_cast<MemEventInit*>(m_linkCPU->recvInitData())) {
            MemEventInit *res   =   new MemEventInit(this->getName(), MemHierarchy::MemEventInit::InitCommand::Region);
            res->setDst(ev->getSrc());
            m_linkCPU->sendInitData(res);
            delete ev;
        }
}

c_Cache::~c_Cache(){}


c_Cache::c_Cache() :
        Component(-1) {
    // for serialization only
}


bool c_Cache::clockTic(Cycle_t clock)
{
    m_simCycle++;
    storeContent();
    eventProcessing();

    return false;
}


/*
 * CPU request handler :
 * Insert request to a request queue
 */
void c_Cache::handleCpuEvent(SST::Event *ev) {

    MemEvent *newReq    =   dynamic_cast<MemEvent *>(ev);
    assert(newReq->getCmd()==Command::GetS || newReq->getCmd()==Command::GetX);

    #ifdef __SST_DEBUG_OUTPUT__
    output->verbose(CALL_INFO,1,0,"[c_Cache] cycle: %lld paddr: %llx, vaddr:%llx, isWrite:%d\n",
            m_simCycle,newReq->getAddr(), newReq->getBaseAddr(),newReq->getCmd()==Command::GetX);
    #endif
    uint64_t issue_cycle = m_simCycle + m_cacheLatency;
    std::pair<uint64_t, MemEvent *> req_entry(issue_cycle, newReq);
    m_cpuReqQ.push_back(req_entry);
}



/*
 * Processing events received from CPU
 */
void c_Cache::eventProcessing()
{
    while(!m_cpuReqQ.empty()&& m_cpuReqQ.front().first<=m_simCycle) {
        MemEvent* newReq    =   m_cpuReqQ.front().second;
        m_cpuReqQ.pop_front();
        uint64_t req_addr       =   newReq->getAddr();
        bool isWrite        =   (newReq->getCmd()==Command::GetX);
        bool isHit          =   m_cache->isHit(req_addr, isWrite);
        if(enableAllHit)
            isHit=true;

        #ifdef __SST_DEBUG_OUTPUT__
        output->verbose(CALL_INFO, 1, 0, "[%lld] addr: %llx write:%d isHit:%d accesses:%lld\n", m_simCycle, req_addr, isWrite, isHit,s_accesses->getCollectionCount());
        #endif


        ///// cache miss /////
        if (!isHit) {
            MCache_Entry victim     =   m_cache->install(req_addr, isWrite);

            //if dirty, store the victim to the memory
            if (victim.dirty) {
                //send a writeback request to the memory
                uint64_t  l_victim_addr      =   m_cache->getAddrFromTag(victim.tag);  //restore cacheline address from the tag
                c_Transaction *wbTxn    =   new c_Transaction(m_seqnum++, e_TransactionType::WRITE, l_victim_addr, 1);
                c_TxnReqEvent *wbev     =   new c_TxnReqEvent();
                wbev->m_payload         =   wbTxn;
                m_linkMem->send(wbev);

                #ifdef __SST_DEBUG_OUTPUT__
                output->verbose(CALL_INFO, 1, 0, "[%lld] victim writeback addr: %llx seqnum:%lld dirty victim\n", m_simCycle, l_victim_addr, wbTxn->getSeqNum());
                #endif
            }

            //send a read request to memory
            c_Transaction *fillTxn      =   new c_Transaction(m_seqnum, e_TransactionType::READ, req_addr, 1);
            c_TxnReqEvent *fillev       =   new c_TxnReqEvent();
            fillev->m_payload           =   fillTxn;
            m_linkMem->send(fillev);

            #ifdef __SST_DEBUG_OUTPUT__
            output->verbose(CALL_INFO, 1, 0, "[%lld] req addr: %llx seqnum:%lld\n", m_simCycle, req_addr,
                            fillTxn->getSeqNum());
            #endif

            // store response events in the response queue to keep track of the outstanding memory requests.
            // When response comes from the memory, the request is removed from the queue
            MemEvent *res               =   new MemEvent(this, req_addr, newReq->getVirtualAddress(), isWrite ? Command::GetX : Command::GetS);
            res->setResponse(newReq);
            m_cpuResQ[m_seqnum]=res;
            m_seqnum++;

        }/////cache hit //////
        else {
            //queue the response to the cpu response queue
            MemEvent *res = new MemEvent(this, req_addr, newReq->getVirtualAddress(),
                                         isWrite ? Command::GetX : Command::GetS);
            res->setResponse(newReq);

            //send a response to cpu
            m_linkCPU->send(res);

            #ifdef __SST_DEBUG_OUTPUT__
            output->verbose(CALL_INFO, 1, 0, "[%lld] hit req addr: %llx\n", m_simCycle, req_addr);
            #endif
        }

        delete newReq;

        if(!isHit)
            s_miss->addData(1);
        else
            s_hit->addData(1);

        if (isWrite)
            s_writeRecv->addData(1);
        else
            s_readRecv->addData(1);

        s_accesses->addData(1);

    }
}

/*
 * Memory Response Handler
 */
void c_Cache::handleMemEvent(SST::Event *ev) {
    c_TxnResEvent* l_newRes     =   dynamic_cast<c_TxnResEvent*>(ev);
    c_Transaction* newTxn       =   l_newRes->m_payload;

    #ifdef __SST_DEBUG_OUTPUT__
    output->verbose(CALL_INFO,1,0,"[%lld] res from memory addr: %llx seqnum:%lld, isWrite:%d\n",m_simCycle,newTxn->getAddress(),newTxn->getSeqNum(),newTxn->isWrite());
    #endif

    if(!newTxn->isWrite())
    {
        assert(m_cpuResQ.find(newTxn->getSeqNum())!=m_cpuResQ.end());

        MemEvent* newRes        =   m_cpuResQ[newTxn->getSeqNum()];
        m_linkCPU->send(newRes);
        m_cpuResQ.erase(newTxn->getSeqNum());
    }
    delete newTxn;
    delete ev;
}

uint64_t clock_tmp=0;
//void c_ControllerPCA::handleContentEvent(SST::Event *ev)
void c_Cache::storeContent()
{
    clock_tmp++;
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
               
                printf("c_Cache] cacheline addr:%llx comp_ratio:%d\n",cacheline_addr,compRatio_vector[0]);

            } else
            {
                fprintf(stderr,"[c_Cache] cpu command error!\n");
                exit(1);
            }

            delete req;
        }
    }
}

/*return normalized size of the compressed cacheline
  0 <= normalized size <= 100
*/
int c_Cache::getCompressedSize(uint64_t addr)
{

    //calculate the compressed size of cacheline
    if(compRatio_bdi.size()>0) {
        int64_t cacheline_addr = (addr >> 6) << 6;
        if(compRatio_bdi.find(cacheline_addr)==compRatio_bdi.end()) {
                printf("Error!! cacheline is not found, %llx\n",addr);
                compRatio_bdi[cacheline_addr] = 0;
                s_BackingMiss->addData(1);
        }

        int normalized_size=compRatio_bdi[cacheline_addr];   // 0 <= normalized_size <= 100

        m_normalized_size[normalized_size]++;
        return normalized_size;
    }
    return -1;
}



void c_Cache::finish()
{
    uint64_t compressed_size_50;
    uint64_t compressed_size_100;

    for(int i=0;i<=50;i++)
    {
        compressed_size_50+=m_normalized_size[i];
    }

     for(int i=51;i<=100;i++)
    {
        compressed_size_100+=m_normalized_size[i];
    }
    /*for(int i=0;i<100;i++)
    {
        printf("compressed_size_count: %d/%lld\n",i,m_normalized_size[i]);
        normalized_size_sum+=i*m_normalized_size[i];
        cnt+=m_normalized_size[i];
    }*/
    fprintf(stderr,"compressed_size<=50 : %lld\n",compressed_size_50);
    fprintf(stderr,"compressed_size>50 : %lld\n",compressed_size_100);
}

