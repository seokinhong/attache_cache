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
#include "c_PageAllocator.hpp"


#include <bitset>

#include <cstdlib>

#include <cstdlib>
#include <iostream>
#include <ctime>


using namespace std;
using namespace SST;
using namespace SST::n_Bank;

c_PageAllocator::c_PageAllocator(ComponentId_t x_id, Params &params):Component(x_id) {
    //*------ get parameters ----*//
    bool l_found=false;

    int verbosity = params.find<int>("verbose", 0);
    output = new SST::Output("CramSim.PageAllocator[@f:@l:@p] ",
                             verbosity, 0, SST::Output::STDOUT);


    isPageAllocLink      = params.find<bool>("isPageAllocLink", false);
    corenum = params.find<int>("corenum", false);
    isMultiThreadMode      = params.find<bool>("isMultiThreadMode", false);
    m_osPageSize      = params.find<uint64_t>("pagesize", false);
    m_memSize      = params.find<uint64_t>("memsize", false);
    m_nextPageAddress = 0;

    if(isPageAllocLink==true) {
        for (int i = 0; i < corenum; i++) {
            string l_linkName = "pageLink_" + to_string(i);
            Link *l_link = configureLink(l_linkName,
                                         new Event::Handler<c_PageAllocator>(this,
                                                                           &c_PageAllocator::handlePageAllocation));

            if (l_link) {
                m_pageLinks.push_back(l_link);
                cout << l_linkName << " is connected" << endl;
            } else {
                cout << l_linkName << " is not found.. exit" << endl;
                exit(-1);
            }
        }
    }
    std::srand(std::time(nullptr)); // use current time as seed for random generator
    std::string l_clockFreqStr = (std::string)params.find<std::string>("ClockFreq", "1GHz", l_found);
    //set our clock
    registerClock(l_clockFreqStr,
                  new Clock::Handler<c_PageAllocator>(this, &c_PageAllocator::clockTic));

    s_numAllocPages=registerStatistic<uint64_t>("numAllocPages");
}

bool c_PageAllocator::clockTic(Cycle_t)
{
    return false;
}

c_PageAllocator::~c_PageAllocator(){}

c_PageAllocator::c_PageAllocator() :
        Component(-1) {
    // for serialization only
}


void c_PageAllocator::handlePageAllocation(SST::Event* event)
{
    SST::MemHierarchy::MemEvent* req=dynamic_cast<SST::MemHierarchy::MemEvent*>(event);
    uint64_t req_addr=req->getAddr();

    if(req->getCmd()==MemHierarchy::Command::Get) //handle requests for physical page number
    {
        std::vector<uint8_t> resp_data;
        uint64_t next_page_num=0;

        uint64_t virtAddr = req_addr;

        next_page_num=getPageAddress(virtAddr);


        //send physical address
        for(int i=0;i<8;i++)
        {
            uint8_t tmp=(uint8_t)(next_page_num>>8*i);
            resp_data.push_back(tmp);
        }

        #ifdef __SST_DEBUG_OUTPUT__
        output->verbose(CALL_INFO,1,0,"requester:%s requested address:%llx allocated page:%llx\n",req->getRqstr().c_str(),req_addr,next_page_num);
        #endif

        SST::MemHierarchy::MemEvent* res=new SST::MemHierarchy::MemEvent(this, req_addr,req_addr,MemHierarchy::Command::GetSResp, resp_data);
        event->getDeliveryLink()->send(res);
    } else
    {
        fprintf(stderr,"[memory controller] paga allocation command error!\n");
        exit(1);
    }

    delete req;
}



uint64_t c_PageAllocator::getPageAddress(uint64_t  virtAddr){

    uint64_t l_nextPageAddress = m_nextPageAddress;
    if(isMultiThreadMode==true) {

        //get physical address
        const uint64_t pageOffset = virtAddr % m_osPageSize;
        const uint64_t virtPageStart = virtAddr - pageOffset;

        std::map<uint64_t, uint64_t>::iterator findEntry = pageTable.find(virtPageStart);

        if (findEntry != pageTable.end()) {
            l_nextPageAddress = findEntry->second;
        }
        else {
            uint64_t nextAddress_tmp = rand()%m_memSize;
            uint64_t offset = nextAddress_tmp % m_osPageSize;
            l_nextPageAddress=nextAddress_tmp-offset;


            while(allocatedPage.end()!=allocatedPage.find(l_nextPageAddress))
            {
                uint64_t nextAddress_tmp = rand()%m_memSize;
                uint64_t offset = nextAddress_tmp % m_osPageSize;
                l_nextPageAddress=nextAddress_tmp-offset;
            }

            allocatedPage[l_nextPageAddress]=1;
            pageTable[virtPageStart]=l_nextPageAddress;

            s_numAllocPages->addData(1);

            if (l_nextPageAddress + m_osPageSize > m_memSize) {
                fprintf(stderr, "[memController] Out of Address Range!!, nextPageAddress:%lld pageSize:%lld memsize:%lld\n", l_nextPageAddress,m_osPageSize,m_memSize);
                fflush(stderr);
                exit(-1);
            }
        }
    }
    else {

        uint64_t nextAddress_tmp = rand()%m_memSize;
        uint64_t offset = nextAddress_tmp % m_osPageSize;
        l_nextPageAddress=nextAddress_tmp-offset;


        while(allocatedPage.end()!=allocatedPage.find(l_nextPageAddress))
        {
            uint64_t nextAddress_tmp = rand()%m_memSize;
            uint64_t offset = nextAddress_tmp % m_osPageSize;
            l_nextPageAddress=nextAddress_tmp-offset;
        }

        //printf("m_nextPageAddress:%llx\n",l_nextPageAddress);
        allocatedPage[l_nextPageAddress]=1;

        s_numAllocPages->addData(1);

        if (l_nextPageAddress + m_osPageSize > m_memSize) {
            fprintf(stderr, "[memController] Out of Address Range!!, nextPageAddress:%lld pageSize:%lld memsize:%lld\n", l_nextPageAddress,m_osPageSize,m_memSize);
            fflush(stderr);
            exit(-1);
        }

    }

    return l_nextPageAddress;
}
