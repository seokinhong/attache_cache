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
    int verbosity = x_params.find<int>("verbose", 0);
    output = new SST::Output("[CramSim, @f:@l:@p] ",
                             verbosity, 0, SST::Output::STDOUT);

    // Set up backing store if needed
    uint64_t memsize = x_params.find<uint64_t>("memsize",10000,l_found);
    if(!l_found)
    {
        fprintf(stderr,"[c_MemhBridgeContent] memsize value is missing!! exit\n");
        exit(1);
    }
    backing_ = (uint8_t*)malloc(memsize);
    m_compEngine = new c_CompressEngine(verbosity,true);

/*
    std::string memoryFile = x_params.find<std::string>("memory_file", "no_string_defined", l_found);
    if ( ! x_params.find<bool>("do_not_back",false)  ) {
        if(l_found)
            backing_ = new Backing(memoryFile.c_str(), memsize);
        else{
            fprintf(stderr,"memory_file name error!\n");
            exit(1);
        }
    }

*/


    /*---- CONFIGURE LINKS ----*/

    // Content links
    m_contentLink = configureLink( "contentLink",new Event::Handler<c_MemhBridgeContent>(this,&c_MemhBridgeContent::handleContentEvent) );

}


c_MemhBridgeContent::~c_MemhBridgeContent() {
    uint64_t cnt=0;
    uint64_t normalized_size_sum=0;
    for(int i=0;i<100;i++)
    {
        printf("compressed_size_count: %d/%lld\n",i,m_normalized_size[i]);
        normalized_size_sum+=i*m_normalized_size[i];
        cnt+=m_normalized_size[i];
    }
    double avg_normalized_size = (double)normalized_size_sum / (double)cnt;
    printf("cacheline compression ratio: %lf\n",(double)1/(double)avg_normalized_size*100);

    free(backing_);

}



void c_MemhBridgeContent::createTxn() {
    uint64_t l_cycle = m_simCycle;

    SST::Event* e = 0;
    while((e = m_linkCPU->recv())) {
        MemReqEvent *event = dynamic_cast<MemReqEvent *>(e);

        c_Transaction *mTxn;
        uint64_t addr = (event->getAddr()>>6)<<6;

        uint8_t* cacheline=(uint8_t*)malloc(sizeof(uint8_t)*64);
        memcpy(cacheline, backing_+addr,64);
        for(int i=0;i<8;i++)
        {
            uint64_t cacheline_tmp=*(uint64_t*)(cacheline+i*8);
            output->verbose(CALL_INFO, 1, 0, "paddr: %llx data: %llx \n", addr + i * 8, cacheline[i]);
        }

        int compressed_size = m_compEngine->getCompressedSize(cacheline,COMP_ALG::BDI);
        int normalized_size = (int)((double)compressed_size/(double)512*100);
        m_normalized_size[normalized_size]++;


        free(cacheline);


        if (event->getIsWrite())
            mTxn = new c_Transaction(event->getReqId(), e_TransactionType::WRITE, addr, 1);
        else
            mTxn = new c_Transaction(event->getReqId(), e_TransactionType::READ, addr, 1);

        std::pair<c_Transaction*, uint64_t > l_entry = std::make_pair(mTxn,l_cycle);
        m_txnReqQ.push_back(l_entry);

        if(k_printTxnTrace)
            printTxn(event->getIsWrite(),addr);


    }
}


void c_MemhBridgeContent::handleContentEvent(SST::Event *ev)
{
    SST::MemHierarchy::MemEvent* req=dynamic_cast<SST::MemHierarchy::MemEvent*>(ev);
    uint64_t cacheline_addr= (req->getAddr()>>6)<<6;
    uint64_t cacheline_vaddr = (req->getVirtualAddress()>>6)<<6;
    uint8_t* mem_ptr_8 = (uint8_t*)malloc(8);
    uint64_t* mem_ptr_64;

    for(int i=0;i<req->getSize();i++)
    {
        backing_[cacheline_addr+i]=req->getPayload()[i];
    }

    uint32_t offset=0;
    for(int j=0;j<8;j++) {
        for (int i = 0; i < 8; i++) {
            //*(mem_ptr_8 + i) = backing_->get(cacheline_addr + offset);
            *(mem_ptr_8 + i) = backing_[cacheline_addr + offset];
            offset++;
        }
        mem_ptr_64=(uint64_t*)mem_ptr_8;
        output->verbose(CALL_INFO, 1, 0, "paddr: %llx vaddr: %llx data: %llx \n",cacheline_addr+j*8, cacheline_vaddr+j*8,*mem_ptr_64);
    }
}



// Element Libarary / Serialization stuff
