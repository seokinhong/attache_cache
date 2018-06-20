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

//SST includes
#include "sst_config.h"

#include <assert.h>
#include <iostream>


//local includes
#include "c_MemhBridgeContent.hpp"
#include "c_TxnReqEvent.hpp"
#include "c_TxnResEvent.hpp"
#include "c_TokenChgEvent.hpp"
#include "memReqEvent.hpp"
#include "c_TxnGen.hpp"
#include <sst/elements/memHierarchy/memEvent.h>
#include <sst/elements/memHierarchy/memEventBase.h>
#include <zlib.h>
#include "c_CompressEngine.hpp"

using namespace SST;
using namespace SST::n_Bank;
using namespace SST::MemHierarchy;

c_MemhBridgeContent::c_MemhBridgeContent(ComponentId_t x_id, Params& x_params) :
        c_MemhBridge(x_id, x_params) {

    /*---- LOAD PARAMS ----*/

    //used for reading params
    bool l_found = true;

    // internal params
    verbosity = x_params.find<int>("verbose", 0);
    output = new SST::Output("[CramSim, @f:@l:@p] ",
                             verbosity, 0, SST::Output::STDOUT);

    k_traceDebug = (bool) x_params.find<bool>("boolTraceDebug", 0, l_found);
    k_loopback_mode = (bool) x_params.find<bool>("boolLoopback", 0, l_found);
    if(k_loopback_mode)
        std::cout<<"[MemhBridgeContent] loopback mode is enabled"<<std::endl;

    m_compEngine = new c_CompressEngine(verbosity,true);

    /*---- CONFIGURE LINKS ----*/

    // Content links
    m_contentLink = configureLink( "contentLink",new Event::Handler<c_MemhBridgeContent>(this,&c_MemhBridgeContent::handleContentEvent) );
    m_numThread = x_params.find<int>("numThread",1);
    boolStoreContent = x_params.find<bool>("boolStoreContent",false);
    boolStoreCompRate = x_params.find<bool>("boolStoreCompRate",false);

    for(int i=0;i<COMP_ALG::COMP_ALG_NUM;i++) {
        m_sumCompRate[(COMP_ALG)i] = 0;
    }
    m_cntReqs=0;

    for(int i=0;i<m_numThread;i++)
    {
        gzFile tmpTraceZ;
        tmpTraceZ = gzopen((k_txnTraceFileName+std::to_string(i)+string(".gz")).c_str(), "wb");
        traceZ.push_back(tmpTraceZ);
    }

}

/*
void c_MemhBridgeContent::init(unsigned int phase) {

    if(!phase)
    {
        MemEventInit * memEvent = new MemEventInit(this->getName(),MemEventInit::InitCommand::Coherence);
        m_linkCPU->sendInitData(new MemEventInit(*memEvent));
    }

    Event *ev;
    while((ev = m_linkCPU->recvInitData())!=NULL) {
        delete ev;
    }
}*/


c_MemhBridgeContent::~c_MemhBridgeContent() {
    uint64_t cnt = 0;
    uint64_t normalized_size_sum = 0;
    if(boolStoreCompRate) {
        for (int i = 0; i < COMP_ALG::COMP_ALG_NUM; i++) {
            double avg_normalized_size = (double) m_sumCompRate[(COMP_ALG)i] / (double) m_cntReqs ;

            if(i==COMP_ALG::BDI)
                printf("bdi compression ratio: %lf avg_normalized_size:%lf sumCompRate:%lld cntReqs:%lld\n", (double) 100 / avg_normalized_size,avg_normalized_size,m_sumCompRate[(COMP_ALG)i],m_cntReqs);
        }
    }

    //free cacheline data in the backing store
    for (std::map<uint64_t, c_Cacheline *>::iterator it = backing_.begin(); it != backing_.end(); ++it)
        if (it->second)
            delete it->second;

    //close trace file
    for(auto &it: traceZ) {
        gzclose(it);
    }
}

void c_MemhBridgeContent::createTxn() {
    uint64_t l_cycle = m_simCycle;

    SST::Event* e = 0;
    while((e = m_linkCPU->recv())) {
        MemEvent *event = dynamic_cast<MemEvent *>(e);

        uint64_t addr = (event->getAddr()>>6)<<6;
        char thread_num=event->getVirtualAddress();


        //get instruction number
        uint64_t pre_inst_num=0;
        if(m_inst_num.find(thread_num)!=m_inst_num.end())
            pre_inst_num=m_inst_num[thread_num];

        if(event->getInstructionPointer()>0)
            m_inst_num[thread_num]=event->getInstructionPointer();

        if(m_inst_num[thread_num]<pre_inst_num)
        {
            printf("thread num: %d pre_inst_num:%lld m_inst_num:%lld\n",thread_num, pre_inst_num,m_inst_num[thread_num]);
        }

        //record trace
        printTxn(l_cycle, thread_num, event->isWriteback(), addr, m_inst_num[thread_num]);



        //send response to the CPU-side components for read request,
        //do not send response for write request
        if(!event->isWriteback()) {
            MemEvent *res = new MemEvent(this, event->getAddr(), event->getBaseAddr(), event->getCmd());
            res->setResponse(event);
            m_linkCPU->send(res);
        }

        delete event;
    }
}


void c_MemhBridgeContent::handleContentEvent(SST::Event *ev)
{
    storeContent(ev);
    delete ev;
}


void c_MemhBridgeContent::storeContent(SST::Event *ev)
{
        SST::MemHierarchy::MemEvent* req=dynamic_cast<SST::MemHierarchy::MemEvent*>(ev);


        uint64_t req_addr=req->getAddr();
        if(req->getCmd()==MemHierarchy::Command::Put) {
            uint64_t cacheline_addr = (req_addr >> 6) << 6;

            //store compression ratio of memory content
            if(boolStoreCompRate) {

                uint64_t *mem_ptr_64;
                int size=req->getSize();
                c_Cacheline* new_cacheline=new c_Cacheline(req->getPayload());

                int compressed_size = m_compEngine->getCompressedSize(new_cacheline->getData(), COMP_ALG::BDI);
                uint8_t normalized_size = (uint8_t) ((double) compressed_size / (double) 512 * 100);

                compratio_bdi[cacheline_addr] = normalized_size;

                if(verbosity>=2) {
                    uint64_t cacheline_vaddr = (req->getVirtualAddress() >> 6) << 6;
                    uint32_t offset = 0;
                    for (int j = 0; j < 8; j++) {
                        mem_ptr_64 = (uint64_t *) new_cacheline->getData();
                        output->verbose(CALL_INFO, 2, 0, "paddr: %llx vaddr: %llx data: %llx \n", cacheline_addr + j * 8,
                                        cacheline_vaddr + j * 8, mem_ptr_64[j]);

                    }
                }
                delete new_cacheline;
            }

            //store memory content
            if(boolStoreContent) {

                if (backing_.find(cacheline_addr) != backing_.end())
                    delete backing_[cacheline_addr];

                c_Cacheline *new_cacheline = new c_Cacheline(req->getPayload());
                backing_[cacheline_addr] = new_cacheline;
            }

        } else
        {
            fprintf(stderr,"[c_ControllerPCA] cpu command error!\n");
            exit(1);
        }
}



void c_MemhBridgeContent::printTxn(uint64_t x_thread_id, bool x_isWrite, uint64_t x_addr,uint64_t x_inst_num,uint32_t compratio_bdi){
    std::string l_txnType;

    uint64_t l_currentCycle = m_simCycle;
    if(x_isWrite)
        l_txnType="P_MEM_WR";
    else
        l_txnType="P_MEM_RD";

    (*m_txnTraceStream) << x_inst_num
                        << " " << std::hex <<x_addr
                        << " " << compratio_bdi
                        << " "  << l_txnType ;

    (*m_txnTraceStream) <<std::endl;
}



void c_MemhBridgeContent::printTxn(uint64_t x_cycle, uint64_t x_thread_id, bool x_isWrite, uint64_t x_addr,uint64_t x_inst_num){
    std::string l_txnType;

    uint64_t cacheline_addr = (x_addr>> 6) << 6;
    uint64_t *data = NULL;
    uint32_t compRate_bdi=0;

    if(boolStoreContent) {
        c_Cacheline *cacheline= backing_[cacheline_addr];
        data =  (uint64_t*)(cacheline->getData());
    }

    if(boolStoreCompRate){
        compRate_bdi= compratio_bdi[cacheline_addr];
        m_sumCompRate[COMP_ALG::BDI]+=compRate_bdi;
        //printf(" m_sumCompRate[COMP_ALG::BDI]:%lld compRate:%d\n",m_sumCompRate[COMP_ALG::BDI],compRate_bdi);
        m_cntReqs++;
    }


    if(k_traceDebug) {
        uint64_t l_currentCycle = m_simCycle;
        if (x_isWrite)
            l_txnType = "P_MEM_WR";
        else
            l_txnType = "P_MEM_RD";

        (*m_txnTraceStream) << "thread"<<std::dec<<x_thread_id
                            << " " <<std::dec<<x_cycle
                            << " " << x_inst_num
                            << " " << std::hex << x_addr
                            << " " << std::dec<< l_txnType;

        if(boolStoreContent) {
            for (int i = 0; i < 8; i++) {
                (*m_txnTraceStream) << " " << std::hex << data[i];
            }
        }

        if(boolStoreCompRate){
            (*m_txnTraceStream) << " "<<std::dec<<compRate_bdi;
        }

        (*m_txnTraceStream) << std::endl;
    }

    uint64_t accumulated_size=sizeof(x_cycle)+sizeof(x_isWrite)+sizeof(x_addr)+sizeof(uint32_t)+sizeof(x_inst_num);
    if(boolStoreContent)
        accumulated_size+=sizeof(uint64_t)*8;
    if(boolStoreCompRate)
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
    memcpy(recorder_buffer+buffer_idx,&x_cycle,sizeof(uint64_t));
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

    if(boolStoreContent) {
        memcpy(recorder_buffer + buffer_idx, data, 64);
        buffer_idx += 64;
    }

    if(boolStoreCompRate) {
        memcpy(recorder_buffer + buffer_idx, &compRate_bdi, sizeof(uint8_t));
        buffer_idx += sizeof(uint8_t);
    }

    gzwrite(traceZ[x_thread_id], recorder_buffer, accumulated_size);
    string myString(recorder_buffer, accumulated_size);

    delete recorder_buffer;
}


// Element Libarary / Serialization stuff
