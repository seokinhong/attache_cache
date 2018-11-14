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
#include "Cache.hpp"




using namespace SST::MemHierarchy;
using namespace SST::CACHE;

namespace SST{
    namespace n_Bank{

         class c_Cache : public SST::Component {
         public:
             c_Cache( ComponentId_t id, Params& params);
             ~c_Cache();
             void init(unsigned int phase);
             void finish();


         private:
             c_Cache();

             virtual bool clockTic(Cycle_t);
             void handleCpuEvent(SST::Event *ev);       //cpu event handler (cpu --> l3 cache)
             void handleMemEvent(SST::Event *ev);       //memory event handler (memory --> controller component of cramsim)
             void eventProcessing();
             void storeContent();
             int getCompressedSize(uint64_t addr);

             //compression ratio storage
             std::map<uint64_t, uint8_t> compRatio_bdi;

             SCache* m_cache;
             SimTime_t  m_simCycle;
             uint32_t   m_cacheLatency;
             bool       enableAllHit;
             uint32_t   m_seqnum;

             //link to/from Memory
             SST::Link* m_linkMem;
             //link to/from CPU
             SST::Link* m_linkCPU;

             // link for getting data content (compression info) from core
             std::vector<SST::Link*> m_laneLinks;


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
             std::map<int, uint64_t> m_normalized_size;
             Statistic<uint64_t>* s_accesses;
             Statistic<uint64_t>* s_hit;
             Statistic<uint64_t>* s_miss;
             Statistic<uint64_t>* s_readRecv;
             Statistic<uint64_t>* s_writeRecv;
             Statistic<uint64_t>* s_BackingMiss;
        };
    }
}

#endif //C_TXNDISPATCHER_HPP
