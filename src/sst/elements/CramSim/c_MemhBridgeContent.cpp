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
#include <zlib.h>
#include "c_CompressEngine.hpp"

using namespace SST;
using namespace SST::n_Bank;
using namespace CramSim;

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
    compression_en=false;

    /*---- CONFIGURE LINKS ----*/

    // Content links
    m_contentLink = configureLink( "contentLink",new Event::Handler<c_MemhBridgeContent>(this,&c_MemhBridgeContent::handleContentEvent) );
    m_numThread = x_params.find<int>("numThread",1);

    for(int i=0;i<m_numThread;i++)
    {
        gzFile tmpTraceZ;
        tmpTraceZ = gzopen((k_txnTraceFileName+std::to_string(i)+string(".gz")).c_str(), "wb");
        traceZ.push_back(tmpTraceZ);
    }

    m_inst_num=0;
}


c_MemhBridgeContent::~c_MemhBridgeContent() {
    uint64_t cnt = 0;
    uint64_t normalized_size_sum = 0;
    if(compression_en) {
        for (int i = 0; i < 100; i++) {
            printf("compressed_size_count: %d/%lld\n", i, m_normalized_size[i]);
            normalized_size_sum += i * m_normalized_size[i];
            cnt += m_normalized_size[i];
        }
        double avg_normalized_size = (double) normalized_size_sum / (double) cnt;
        printf("cacheline compression ratio: %lf\n", (double) 1 / (double) avg_normalized_size * 100);
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
        MemReqEvent *event = dynamic_cast<MemReqEvent *>(e);

        c_Transaction *mTxn;
        uint64_t addr = (event->getAddr()>>6)<<6;



        if(k_printTxnTrace) {
            if(backing_.find(addr)!=backing_.end()) {
                //get cacheline data
                c_Cacheline *cl = backing_[addr];
                printTxn(l_cycle, 0, event->getIsWrite(), addr, m_inst_num, cl->getData());
            }
        }

        if(k_loopback_mode)
        {
            //create txn
            if (event->getIsWrite()) {
                s_writeTxnSent->addData(1);
                m_reqWriteCount++;
                mTxn = new c_Transaction(event->getReqId(), e_TransactionType::WRITE, addr, 1);
            }
            else {
                s_readTxnSent->addData(1);
                m_reqReadCount++;
                mTxn = new c_Transaction(event->getReqId(), e_TransactionType::READ, addr, 1);
            }
            m_txnResQ.push_back(mTxn);

        } else {
            //create txn
            if (event->getIsWrite())
                mTxn = new c_Transaction(event->getReqId(), e_TransactionType::WRITE, addr, 1);
            else
                mTxn = new c_Transaction(event->getReqId(), e_TransactionType::READ, addr, 1);

            std::pair<c_Transaction *, uint64_t> l_entry = std::make_pair(mTxn, l_cycle);
            m_txnReqQ.push_back(l_entry);
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
        m_inst_num=req->getInstNum();

        uint64_t req_addr=req->getAddr();
        if(req->getCmd()==MemHierarchy::Command::Put) {
            uint64_t cacheline_addr = (req_addr >> 6) << 6;

            if(compression_en) {

                uint64_t *mem_ptr_64;
                int size=req->getSize();
                c_Cacheline* new_cacheline=new c_Cacheline(req->getPayload());

                int compressed_size = m_compEngine->getCompressedSize(new_cacheline->getData(), COMP_ALG::BDI);
                int normalized_size = (int) ((double) compressed_size / (double) 512 * 100);

                compratio_bdi[cacheline_addr] = normalized_size;

                if(verbosity>2) {
                    uint64_t cacheline_vaddr = (req->getVirtualAddress() >> 6) << 6;
                    uint32_t offset = 0;
                    for (int j = 0; j < 8; j++) {
                        mem_ptr_64 = (uint64_t *) new_cacheline->getData();
                        output->verbose(CALL_INFO, 4, 0, "paddr: %llx vaddr: %llx data: %llx \n", cacheline_addr + j * 8,
                                        cacheline_vaddr + j * 8, *mem_ptr_64);
                    }
                }
                delete new_cacheline;
            }
            else
            {
                    std::vector<uint8_t> recv_data = req->getPayload();
                    uint64_t compressed_size = 0;

                    if(backing_.find(cacheline_addr)!=backing_.end())
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



void c_MemhBridgeContent::printTxn(uint64_t x_cycle, uint64_t x_thread_id, bool x_isWrite, uint64_t x_addr,uint64_t x_inst_num,uint8_t* data){
    std::string l_txnType;

    if(k_traceDebug) {
        uint64_t l_currentCycle = m_simCycle;
        if (x_isWrite)
            l_txnType = "P_MEM_WR";
        else
            l_txnType = "P_MEM_RD";

        (*m_txnTraceStream) << x_cycle
                            << " " << std::hex << x_addr
                            << " " << l_txnType;


        for (int i = 0; i < 8; i++) {
            uint64_t *data_64 = (uint64_t *) (data + i * 8);
            (*m_txnTraceStream) << " " << *data_64;
        }

        (*m_txnTraceStream) << " " << x_inst_num;

        (*m_txnTraceStream) << std::endl;
    }

    uint64_t accumulated_size=sizeof(x_cycle)+sizeof(x_isWrite)+sizeof(x_addr)+sizeof(uint32_t)+sizeof(uint64_t)*8+sizeof(x_inst_num);
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
    //operation
    memcpy(recorder_buffer+buffer_idx,&op,sizeof(char));
    buffer_idx+=sizeof(char);
    //address
    memcpy(recorder_buffer+buffer_idx,&x_addr,sizeof(uint64_t));
    buffer_idx+=sizeof(uint64_t);
    //size
    memcpy(recorder_buffer+buffer_idx,&size,sizeof(uint32_t));
    buffer_idx+=sizeof(uint32_t);
    //data (cacheline)
    memcpy(recorder_buffer+buffer_idx,data,64);
    buffer_idx+=64;
    //instruction number
    memcpy(recorder_buffer+buffer_idx,&x_inst_num,sizeof(uint64_t));
    buffer_idx+=sizeof(uint64_t);


    gzwrite(traceZ[x_thread_id], recorder_buffer, accumulated_size);
    delete recorder_buffer;
}


// Element Libarary / Serialization stuff
