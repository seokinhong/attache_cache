// Copyright 2009-2016 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2016, Sandia Corporation
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#include <sst_config.h>
#include <sst/core/link.h>
#include "sst/elements/memHierarchy/util.h"
#include "membackend/traceRecordBackend.h"
#include "sst/elements/CramSim/memReqEvent.hpp"

using namespace SST;
using namespace SST::MemHierarchy;
using namespace SST::CramSim;

traceRecordBackend::traceRecordBackend(Component *comp, Params &params) : SimpleMemBackend(comp, params){
    k_traceDebug =  params.find<bool>("traceDebug", false);

    std::string traceFileName = (std::string) params.find<std::string>("traceFileName", "-");
    int numThread = params.find<int>("numThread",1);

    for(int i=0;i<numThread;i++)
    {
        gzFile tmpTraceZ;
        tmpTraceZ = gzopen((traceFileName+std::to_string(i)+string(".gz")).c_str(), "wb");
        traceZ.push_back(tmpTraceZ);
    }

    //set our clock
    registerClock(this->m_clockFreq,
                  new Clock::Handler<traceRecordBackend>(this, &traceRecordBackend::clock));
}

bool traceRecordBackend::clock(Cycle_t cycle){
    for(auto &it:memReqs)
        handleMemResponse(it);
    memReqs.clear();

    return false;
}


bool traceRecordBackend::issueRequest( ReqId reqId, Addr addr_, bool isWrite ,unsigned numBytes){
    printf("i'm here\n");
    return true;
}

bool traceRecordBackend::issueRequest( ReqId reqId, Addr addr_, bool isWrite ,unsigned numBytes, std::vector<uint64_t> data){

    assert(data.size()==3);
    //memReqs.insert( reqId );

    uint64_t addr = (addr_>>6)<<6;
    uint64_t thread_id = data[0];
    uint64_t inst_pointer = data[1];
    uint64_t compRate = data[2];

    char thread_num=data[0];

    //get instruction number
    uint64_t pre_inst_num=0;
    if(m_inst_num.find(thread_num)!=m_inst_num.end())
        pre_inst_num=m_inst_num[thread_num];

    if(inst_pointer>0)
        m_inst_num[thread_num]=data[1];

    if(m_inst_num[thread_num]<pre_inst_num)
        printf("thread num: %d pre_inst_num:%lld m_inst_num:%lld\n",thread_num, pre_inst_num,m_inst_num[thread_num]);

    //record trace
    recordTrace(thread_num, isWrite, addr,inst_pointer,compRate);

    memReqs.push_back(reqId);

    return true;
}


traceRecordBackend::~traceRecordBackend() {
    uint64_t cnt = 0;
    uint64_t normalized_size_sum = 0;


    //close trace file
    for(auto &it: traceZ) {
        gzclose(it);
    }
}

void traceRecordBackend::recordTrace
        (uint64_t x_thread_id, bool x_isWrite, uint64_t x_addr,uint64_t x_inst_num, uint64_t compRate_bdi){

    uint64_t cacheline_addr = (x_addr>> 6) << 6;
    uint64_t *data = NULL;
    std::string l_txnType;


    uint64_t l_currentCycle = this->getSimulation()->getCurrentSimCycle();
    if(k_traceDebug) {

        if (x_isWrite)
            l_txnType = "P_MEM_WR";
        else
            l_txnType = "P_MEM_RD";

        (std::cout) << "thread"<<std::dec<<x_thread_id
                            << " " <<std::dec<<l_currentCycle
                            << " " << x_inst_num
                            << " " << std::hex << x_addr
                            << " " << l_txnType;

        (std::cout) << " "<<std::dec<<compRate_bdi;
        (std::cout) << std::endl;
    }

    uint64_t accumulated_size=sizeof(l_currentCycle)+sizeof(x_isWrite)+sizeof(x_addr)+sizeof(uint32_t)+sizeof(x_inst_num);
    //add size of dbi
    accumulated_size+=sizeof(uint8_t);

    char* recorder_buffer=(char*)malloc(accumulated_size);
    char op;
    if(x_isWrite)
        op='W';
    else
        op='R';
    uint32_t size=8;

    uint64_t buffer_idx=0;
    //cycle
    memcpy(recorder_buffer+buffer_idx,&l_currentCycle,sizeof(uint64_t));
    buffer_idx+=sizeof(uint64_t);
    //instruction number
    memcpy(recorder_buffer+buffer_idx,&x_inst_num,sizeof(uint64_t));
    buffer_idx+=sizeof(uint64_t);
    //operation
    memcpy(recorder_buffer+buffer_idx,&op,sizeof(char));
    buffer_idx+=sizeof(char);
    //address
    memcpy(recorder_buffer+buffer_idx,&x_addr,sizeof(uint64_t));
    buffer_idx+=sizeof(uint64_t);
    //size
    memcpy(recorder_buffer+buffer_idx,&size,sizeof(uint32_t));
    buffer_idx+=sizeof(uint32_t);


    memcpy(recorder_buffer + buffer_idx, &compRate_bdi, sizeof(uint8_t));
    buffer_idx += sizeof(uint8_t);

    gzwrite(traceZ[x_thread_id], recorder_buffer, accumulated_size);
    string myString(recorder_buffer, accumulated_size);

    delete recorder_buffer;
}