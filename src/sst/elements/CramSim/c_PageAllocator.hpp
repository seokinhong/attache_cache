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

#ifndef C_PAGEALLOCATOR_HPP
#define C_PAGEALLOCATOR_HPP
//SST includes
#include <sst/core/component.h>
#include <sst/core/link.h>
#include <sst/elements/memHierarchy/memEvent.h>
#include <random>

namespace SST{
    namespace n_Bank{

         class c_PageAllocator : public SST::Component {
         public:
             c_PageAllocator( ComponentId_t id, Params& params);
             ~c_PageAllocator();

         private:


            std::mt19937_64 rng;
             void seed(uint64_t new_seed = std::mt19937_64::default_seed) {
                     rng.seed(new_seed);
                 }

             uint64_t randGen() {
                     return rng(); }



             Output *output;
             bool isPageAllocLink;
             bool isMultiThreadMode;
             std::vector<SST::Link*> m_pageLinks;

             int corenum;
             uint64_t  m_nextPageAddress;
             std::map<uint64_t, uint64_t> pageTable;
             std::map<uint64_t, uint64_t> allocatedPage;
             uint64_t m_osPageSize;
             size_t      m_memSize;

             Statistic<uint64_t>* s_numAllocPages;



             void handlePageAllocation(SST::Event* event);
             uint64_t getPageAddress(uint64_t virtAddr);
             uint64_t mapPagesRandom(uint64_t pageSize, uint64_t startAddr);

             virtual bool clockTic(Cycle_t);
             uint64_t m_pagecount;
             c_PageAllocator();

        };
    }
}

#endif //C_TXNDISPATCHER_HPP
