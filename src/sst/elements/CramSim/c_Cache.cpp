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


#define MCACHE_SRRIP_MAX  7
#define MCACHE_SRRIP_INIT 1
#define MCACHE_PSEL_MAX    1023
#define MCACHE_LEADER_SETS  32

using namespace std;
using namespace SST;
using namespace SST::n_Bank;
using namespace SST::MemHierarchy;

c_Cache::c_Cache(ComponentId_t x_id, Params &params):Component(x_id) {
    //*------ get parameters ----*//
    bool l_found=false;

    m_simCycle=0;

    int verbosity = params.find<int>("verbose", 0);
    output = new SST::Output("CramSim.Cache[@f:@l:@p] ",
                             verbosity, 0, SST::Output::STDOUT);

    std::string l_clockFreqStr = (std::string)params.find<std::string>("ClockFreq", "1GHz", l_found);
    enableAllHit = (bool)params.find<bool>("enableAllHit",false);

    //set our clock
    registerClock(l_clockFreqStr,
                  new Clock::Handler<c_Cache>(this, &c_Cache::clockTic));

    uint64_t cache_size = params.find<uint64_t>("cache_size",4*1024*1024); //default:4MB
    uint32_t assoc = params.find<uint32_t>("associativity",16);
    string replacement=params.find<string>("repl_policy","LRU");
    m_cacheLatency=params.find<uint32_t>("latency",20);

    printf("[cramsim cache] cache_size(KB):%d\n", cache_size/1024);
    printf("[cramsim cache] repl_policy:%s\n",replacement.c_str());
    printf("[cramsim cache] latency:%d\n",m_cacheLatency);
    printf("[cramsim cache] enableAllHit? %d\n", enableAllHit);

    uns repl = MCache_ReplPolicy_Enum::REPL_LRU;
    uns sets = cache_size/(assoc*64);

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


    m_cache = mcache_new(sets,assoc,repl);

    //---- configure link ----//
    m_linkMem = configureLink("memLink",new Event::Handler<c_Cache>(this,&c_Cache::handleMemEvent));
    m_linkCPU = configureLink("cpuLink",new Event::Handler<c_Cache>(this,&c_Cache::handleCpuEvent));

    m_seqnum = 0;

    s_accesses = registerStatistic<uint64_t>("accesses");
    s_hit = registerStatistic<uint64_t>("hits");
    s_miss = registerStatistic<uint64_t>("misses");
    s_readRecv = registerStatistic<uint64_t>("reads");
    s_writeRecv = registerStatistic<uint64_t>("writes");

}
void c_Cache::init(unsigned int phase) {

        MemEventInit *ev = NULL;
        while(ev=dynamic_cast<MemEventInit*>(m_linkCPU->recvInitData())) {
            MemEventInit *res=new MemEventInit(this->getName(), MemHierarchy::MemEventInit::InitCommand::Region);
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
    eventProcessing();

    return false;
}


//request from cpu
void c_Cache::handleCpuEvent(SST::Event *ev) {

    MemEvent *newReq = dynamic_cast<MemEvent *>(ev);
    assert(newReq->getCmd()==Command::GetS || newReq->getCmd()==Command::GetX);

    #ifdef __SST_DEBUG_OUTPUT__
    output->verbose(CALL_INFO,1,0,"[c_Cache] cycle: %lld paddr: %llx, vaddr:%llx, isWrite:%d\n",
            m_simCycle,newReq->getAddr(), newReq->getBaseAddr(),newReq->getCmd()==Command::GetX);
    #endif
    uint64_t issue_cycle = m_simCycle + m_cacheLatency;
    std::pair<uint64_t, MemEvent *> req_entry(issue_cycle, newReq);
    m_cpuReqQ.push_back(req_entry);
}


//processing events received from CPU
void c_Cache::eventProcessing()
{
    while(!m_cpuReqQ.empty()&& m_cpuReqQ.front().first<=m_simCycle) {
        MemEvent* newReq=m_cpuReqQ.front().second;
        m_cpuReqQ.pop_front();

        Addr req_addr = (newReq->getAddr() >> 3) << 3;
        bool isWrite = (newReq->getCmd()==Command::GetX);
        bool isHit = mcache_access(m_cache, req_addr, isWrite);

        if(enableAllHit)
            isHit=true;

        #ifdef __SST_DEBUG_OUTPUT__
        output->verbose(CALL_INFO, 1, 0, "[%lld] addr: %llx write:%d isHit:%d accesses:%lld\n", m_simCycle, req_addr, isWrite, isHit,s_accesses->getCollectionCount());
        #endif

        // cache miss
        if (!isHit) {
            MCache_Entry victim = mcache_install(m_cache, req_addr, isWrite);

            //if dirty, store the victim to the memory
            if (victim.dirty) {
                Addr l_victim_addr = victim.tag;
                c_Transaction *wbTxn = new c_Transaction(m_seqnum++, e_TransactionType::WRITE, l_victim_addr, 1);
                c_TxnReqEvent *wbev = new c_TxnReqEvent();
                wbev->m_payload=wbTxn;
                m_linkMem->send(wbev);

                #ifdef __SST_DEBUG_OUTPUT__
                output->verbose(CALL_INFO, 1, 0, "[%lld] victim writeback addr: %llx seqnum:%lld dirty victim\n", m_simCycle, l_victim_addr, wbTxn->getSeqNum());
                #endif
            }

            //send a read request to memory
            c_Transaction *fillTxn = new c_Transaction(m_seqnum, e_TransactionType::READ, req_addr, 1);
            c_TxnReqEvent *fillev = new c_TxnReqEvent();
            fillev->m_payload=fillTxn;
            m_linkMem->send(fillev);

            #ifdef __SST_DEBUG_OUTPUT__
            output->verbose(CALL_INFO, 1, 0, "[%lld] req addr: %llx seqnum:%lld\n", m_simCycle, req_addr,
                            fillTxn->getSeqNum());
            #endif

            // register a response event in the response queue to keep track the corresponding memory request.
            // When response comes from the memory, the request is removed from the queue
            MemEvent *res = new MemEvent(this, req_addr, newReq->getVirtualAddress(), isWrite ? Command::GetX : Command::GetS);
            res->setResponse(newReq);
            m_cpuResQ[m_seqnum]=res;

            m_seqnum++;
        } else //cache hit
        {
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

//handle responses from memory
void c_Cache::handleMemEvent(SST::Event *ev) {
    c_TxnResEvent* l_newRes=dynamic_cast<c_TxnResEvent*>(ev);
    c_Transaction* newTxn=l_newRes->m_payload;

    #ifdef __SST_DEBUG_OUTPUT__
    output->verbose(CALL_INFO,1,0,"[%lld] res from memory addr: %llx seqnum:%lld, isWrite:%d\n",m_simCycle,newTxn->getAddress(),newTxn->getSeqNum(),newTxn->isWrite());
    #endif

    if(!newTxn->isWrite())
    {
        assert(m_cpuResQ.find(newTxn->getSeqNum())!=m_cpuResQ.end());

        MemEvent* newRes=m_cpuResQ[newTxn->getSeqNum()];
        m_linkCPU->send(newRes);
        m_cpuResQ.erase(newTxn->getSeqNum());
    }
    delete newTxn;
    delete ev;
}



////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

MCache *c_Cache::mcache_new(uns sets, uns assocs, uns repl_policy )
{
    MCache *c = (MCache *) calloc (1, sizeof (MCache));
    c->sets    = sets;
    c->assocs  = assocs;
    c->repl_policy = (MCache_ReplPolicy)repl_policy;

    c->entries  = (MCache_Entry *) calloc (sets * assocs, sizeof(MCache_Entry));


    c->fifo_ptr  = (uns *) calloc (sets, sizeof(uns));

    //for drrip or dip
    c_Cache::mcache_select_leader_sets(c,sets);
    c->psel=(MCACHE_PSEL_MAX+1)/2;


    return c;
}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

void c_Cache::mcache_select_leader_sets(MCache *c, uns sets){
    uns done=0;

    c->is_leader_p0  = (Flag *) calloc (sets, sizeof(Flag));
    c->is_leader_p1  = (Flag *) calloc (sets, sizeof(Flag));

    while(done <= MCACHE_LEADER_SETS){
        uns randval=rand()%sets;
        if( (c->is_leader_p0[randval]==FALSE)&&(c->is_leader_p1[randval]==FALSE)){
            c->is_leader_p0[randval]=TRUE;
            done++;
        }
    }

    done=0;
    while(done <= MCACHE_LEADER_SETS){
        uns randval=rand()%sets;
        if( (c->is_leader_p0[randval]==FALSE)&&(c->is_leader_p1[randval]==FALSE)){
            c->is_leader_p1[randval]=TRUE;
            done++;
        }
    }
}



Flag c_Cache::mcache_access(MCache *c, Addr addr, Flag dirty)
{
  Addr  tag  = addr; // full tags
  uns   set  = c_Cache::mcache_get_index(c,addr);
  uns   start = set * c->assocs;
  uns   end   = start + c->assocs;
  uns   ii;

  c->s_count++;

  for (ii=start; ii<end; ii++){
    MCache_Entry *entry = &c->entries[ii];

    if(entry->valid && (entry->tag == tag))
      {
	entry->last_access  = c->s_count;
	entry->ripctr       = MCACHE_SRRIP_MAX;
	c->touched_wayid = (ii-start);
	c->touched_setid = set;
	c->touched_lineid = ii;
	if(dirty==TRUE) //If the operation is a WB then mark it as dirty
	{
	  c_Cache::mcache_mark_dirty(c,tag);
	}
	return HIT;
      }
 }

  //even on a miss, we need to know which set was accessed
  c->touched_wayid = 0;
  c->touched_setid = set;
  c->touched_lineid = start;

  c->s_miss++;
  return MISS;
}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

Flag    c_Cache::mcache_probe    (MCache *c, Addr addr)
{
    Addr  tag  = addr; // full tags
    uns   set  = c_Cache::mcache_get_index(c,addr);
    uns   start = set * c->assocs;
    uns   end   = start + c->assocs;
    uns   ii;

    for (ii=start; ii<end; ii++){
        MCache_Entry *entry = &c->entries[ii];
        if(entry->valid && (entry->tag == tag))
        {
            return TRUE;
        }
    }

    return FALSE;
}


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

Flag    c_Cache::mcache_invalidate    (MCache *c, Addr addr)
{
    Addr  tag  = addr; // full tags
    uns   set  = c_Cache::mcache_get_index(c,addr);
    uns   start = set * c->assocs;
    uns   end   = start + c->assocs;
    uns   ii;

    for (ii=start; ii<end; ii++){
        MCache_Entry *entry = &c->entries[ii];
        if(entry->valid && (entry->tag == tag))
        {
            entry->valid = FALSE;
            return TRUE;
        }
    }

    return FALSE;
}


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

void    c_Cache::mcache_swap_lines(MCache *c, uns set, uns way_ii, uns way_jj)
{
    uns   start = set * c->assocs;
    uns   loc_ii   = start + way_ii;
    uns   loc_jj   = start + way_jj;

    MCache_Entry tmp = c->entries[loc_ii];
    c->entries[loc_ii] = c->entries[loc_jj];
    c->entries[loc_jj] = tmp;

}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

Flag    c_Cache::mcache_mark_dirty    (MCache *c, Addr addr)
{
    Addr  tag  = addr; // full tags
    uns   set  = c_Cache::mcache_get_index(c,addr);
    uns   start = set * c->assocs;
    uns   end   = start + c->assocs;
    uns   ii;

    for (ii=start; ii<end; ii++){
        MCache_Entry *entry = &c->entries[ii];
        if(entry->valid && (entry->tag == tag))
        {
            entry->dirty = TRUE;
            return TRUE;
        }
    }

    return FALSE;
}

////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

MCache_Entry c_Cache::mcache_install(MCache *c, Addr addr, Flag dirty)
{
    Addr  tag  = addr; // full tags
    uns   set  = mcache_get_index(c,addr);
    uns   start = set * c->assocs;
    uns   end   = start + c->assocs;
    uns   ii, victim;

    Flag update_lrubits=TRUE;

    MCache_Entry *entry;
    MCache_Entry evicted_entry;

    for (ii=start; ii<end; ii++){
        entry = &c->entries[ii];
        if(entry->valid && (entry->tag == tag)){
            printf("Installed entry already with addr:%llx present in set:%u\n", addr, set);
            fflush(stdout);
            exit(-1);
        }
    }

    // find victim and install entry
    victim = c_Cache::mcache_find_victim(c, set);
    entry = &c->entries[victim];
    evicted_entry =c->entries[victim];
    if(entry->valid){
        c->s_evict++;
    }

    //udpate DRRIP info and select value of ripctr
    uns ripctr_val=MCACHE_SRRIP_INIT;

    if(c->repl_policy==REPL_DRRIP){
        ripctr_val=mcache_drrip_get_ripctrval(c,set);
    }

    if(c->repl_policy==REPL_DIP){
        update_lrubits=mcache_dip_check_lru_update(c,set);
    }


    //put new information in
    entry->tag   = tag;
    entry->valid = TRUE;
    if(dirty==TRUE)
        entry->dirty=TRUE;
    else
        entry->dirty = FALSE;
    entry->ripctr  = ripctr_val;

    if(update_lrubits){
        entry->last_access  = c->s_count;
    }



    c->fifo_ptr[set] = (c->fifo_ptr[set]+1)%c->assocs; // fifo update

    c->touched_lineid=victim;
    c->touched_setid=set;
    c->touched_wayid=victim-(set*c->assocs);
    return evicted_entry;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
Flag c_Cache::mcache_dip_check_lru_update(MCache *c, uns set){
    Flag update_lru=TRUE;

    if(c->is_leader_p0[set]){
        if(c->psel<MCACHE_PSEL_MAX){
            c->psel++;
        }
        update_lru=FALSE;
        if(rand()%100<5) update_lru=TRUE; // BIP
    }

    if(c->is_leader_p1[set]){
        if(c->psel){
            c->psel--;
        }
        update_lru=1;
    }

    if( (c->is_leader_p0[set]==FALSE)&& (c->is_leader_p1[set]==FALSE)){
        if(c->psel >= (MCACHE_PSEL_MAX+1)/2){
            update_lru=1; // policy 1 wins
        }else{
            update_lru=FALSE; // policy 0 wins
            if(rand()%100<5) update_lru=TRUE; // BIP
        }
    }

    return update_lru;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
uns c_Cache::mcache_drrip_get_ripctrval(MCache *c, uns set){
    uns ripctr_val=MCACHE_SRRIP_INIT;

    if(c->is_leader_p0[set]){
        if(c->psel<MCACHE_PSEL_MAX){
            c->psel++;
        }
        ripctr_val=0;
        if(rand()%100<5) ripctr_val=1; // BIP
    }

    if(c->is_leader_p1[set]){
        if(c->psel){
            c->psel--;
        }
        ripctr_val=1;
    }

    if( (c->is_leader_p0[set]==FALSE)&& (c->is_leader_p1[set]==FALSE)){
        if(c->psel >= (MCACHE_PSEL_MAX+1)/2){
            ripctr_val=1; // policy 1 wins
        }else{
            ripctr_val=0; // policy 0 wins
            if(rand()%100<5) ripctr_val=1; // BIP
        }
    }


    return ripctr_val;
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns c_Cache::mcache_find_victim (MCache *c, uns set)
{
    int ii;
    int start = set   * c->assocs;
    int end   = start + c->assocs;

    //search for invalid first
    for (ii = start; ii < end; ii++){
        if(!c->entries[ii].valid){
            return ii;
        }
    }


    switch(c->repl_policy){
        case REPL_LRU:
            return c_Cache::mcache_find_victim_lru(c, set);
        case REPL_RND:
            return c_Cache::mcache_find_victim_rnd(c, set);
        case REPL_SRRIP:
            return c_Cache::mcache_find_victim_srrip(c, set);
        case REPL_DRRIP:
            return c_Cache::mcache_find_victim_srrip(c, set);
        case REPL_FIFO:
            return c_Cache::mcache_find_victim_fifo(c, set);
        case REPL_DIP:
            return c_Cache::mcache_find_victim_lru(c, set);
        default:
            assert(0);
    }

    return -1;

}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns c_Cache::mcache_find_victim_lru (MCache *c,  uns set)
{
    uns start = set   * c->assocs;
    uns end   = start + c->assocs;
    uns lowest=start;
    uns ii;


    for (ii = start; ii < end; ii++){
        if (c->entries[ii].last_access < c->entries[lowest].last_access){
            lowest = ii;
        }
    }

    return lowest;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns c_Cache::mcache_find_victim_rnd (MCache *c,  uns set)
{
    uns start = set   * c->assocs;
    uns victim = start + rand()%c->assocs;

    return  victim;
}



////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns c_Cache::mcache_find_victim_srrip (MCache *c,  uns set)
{
    uns start = set   * c->assocs;
    uns end   = start + c->assocs;
    uns ii;
    uns victim = end; // init to impossible

    while(victim == end){
        for (ii = start; ii < end; ii++){
            if (c->entries[ii].ripctr == 0){
                victim = ii;
                break;
            }
        }

        if(victim == end){
            for (ii = start; ii < end; ii++){
                c->entries[ii].ripctr--;
            }
        }
    }

    return  victim;
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns c_Cache::mcache_find_victim_fifo (MCache *c,  uns set)
{
    uns start = set   * c->assocs;
    uns retval = start + c->fifo_ptr[set];
    return retval;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns c_Cache::mcache_get_index(MCache *c, Addr addr){
    uns retval;

    switch(c->index_policy){
        case 0:
            retval=addr%c->sets;
            break;

        default:
            exit(-1);
    }

    return retval;
}

