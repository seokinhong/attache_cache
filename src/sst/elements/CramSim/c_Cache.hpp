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

#ifndef C_CACHE_HPP
#define C_CACHE_HPP
//SST includes
#include <sst/core/component.h>
#include <sst/core/link.h>
#include "c_Transaction.hpp"
#include "c_TxnReqEvent.hpp"
#include "c_TxnResEvent.hpp"
#include <sst/elements/memHierarchy/memEvent.h>




using namespace SST::MemHierarchy;

namespace SST{
    namespace n_Bank{
        #define FALSE 0
        #define TRUE  1

        #define HIT   1
        #define MISS  0

        #define MCACHE_SRRIP_MAX  7
        #define MCACHE_SRRIP_INIT 1
        #define MCACHE_PSEL_MAX    1023
        #define MCACHE_LEADER_SETS  32

        typedef unsigned	    uns;
        typedef unsigned char	    uns8;
        typedef unsigned short	    uns16;
        typedef unsigned	    uns32;
        typedef unsigned long long  uns64;
        typedef short		    int16;
        typedef int		    int32;
        typedef int long long	    int64;
        typedef int		    Generic_Enum;

        /* Conventions */
        typedef uns32		    Binary;
        typedef uns8		    Flag;

        typedef uns64               Counter;
        typedef int64               SCounter;

        typedef struct MCache_Entry {
            Flag    valid;
            Flag    dirty;
            Addr    tag;
            uns     ripctr;
            uns64   last_access;
        }MCache_Entry;

        typedef enum MCache_ReplPolicy_Enum {
            REPL_LRU=0,
            REPL_RND=1,
            REPL_SRRIP=2,
            REPL_DRRIP=3,
            REPL_FIFO=4,
            REPL_DIP=5,
            NUM_REPL_POLICY=6
        } MCache_ReplPolicy;

        typedef struct MCache{
            uns sets;
            uns assocs;
            MCache_ReplPolicy repl_policy; //0:LRU  1:RND 2:SRRIP
            uns index_policy; // how to index cache

            Flag *is_leader_p0; // leader SET for D(RR)IP
            Flag *is_leader_p1; // leader SET for D(RR)IP
            uns psel;

            MCache_Entry *entries;
            uns *fifo_ptr; // for fifo replacement (per set)

            uns64 s_count; // number of accesses
            uns64 s_miss; // number of misses
            uns64 s_evict; // number of evictions

            int touched_wayid;
            int touched_setid;
            int touched_lineid;
        } MCache;


         class c_Cache : public SST::Component {
         public:
             c_Cache( ComponentId_t id, Params& params);
             ~c_Cache();
             void init(unsigned int phase);
             static MCache     *mcache_new(uns sets, uns assocs, uns repl );
             static bool       mcache_access (MCache *c, Addr addr, Flag dirty);  //true: hit, false: miss
             static MCache_Entry  mcache_install (MCache *c, Addr addr, Flag dirty);


         private:
             c_Cache();

             virtual bool clockTic(Cycle_t);
             void handleCpuEvent(SST::Event *ev);       //cpu event handler (cpu --> l3 cache)
             void handleMemEvent(SST::Event *ev);       //memory event handler (memory --> controller component of cramsim)
             void eventProcessing();

             static Flag    mcache_probe         (MCache *c, Addr addr);
             static Flag    mcache_invalidate    (MCache *c, Addr addr);
             static Flag    mcache_mark_dirty    (MCache *c, Addr addr);
             static uns     mcache_get_index     (MCache *c, Addr addr);

             static uns     mcache_find_victim   (MCache *c, uns set);
             static uns     mcache_find_victim_lru   (MCache *c, uns set);
             static uns     mcache_find_victim_rnd   (MCache *c, uns set);
             static uns     mcache_find_victim_srrip   (MCache *c, uns set);
             static uns     mcache_find_victim_fifo    (MCache *c, uns set);
             void    mcache_swap_lines(MCache *c, uns set, uns way_i, uns way_j);

             static void    mcache_select_leader_sets(MCache *c,uns sets);
             static uns     mcache_drrip_get_ripctrval(MCache *c, uns set);
             static Flag    mcache_dip_check_lru_update(MCache *c, uns set);


             MCache*    m_cache;
             SimTime_t  m_simCycle;
             uint32_t   m_cacheLatency;
             bool       enableAllHit;
             uint32_t   m_seqnum;

             //link to/from Memory
             SST::Link* m_linkMem;
             //link to/from CPU
             SST::Link* m_linkCPU;

             /*class MEM_REQ{
             public:
                 MEM_REQ(uint64_t t, c_Transaction* x_txn){time=t;txn=x_txn;}
                 uint64_t time;
                 c_Transaction* txn;
             };

             class CPU_RES{
             public:
                 CPU_RES(uint64_t t_, MemEvent* ev_,string dst_, bool ready_)
                 : time(t_),ev(ev_),dst(dst_),ready(ready_) {}
                 uint64_t   time;
                 MemEvent*  ev;
                 string     dst;
                 bool       ready;
             };*/

             // Response and Request Queue
             std::map<uint64_t,MemEvent*>               m_cpuResQ;
             std::deque<std::pair<uint64_t,MemEvent*>>  m_cpuReqQ;

             // Debug Output
             Output* output;

             // Statistics
             Statistic<uint64_t>* s_accesses;
             Statistic<uint64_t>* s_hit;
             Statistic<uint64_t>* s_miss;
             Statistic<uint64_t>* s_readRecv;
             Statistic<uint64_t>* s_writeRecv;
        };
    }
}

#endif //C_TXNDISPATCHER_HPP
