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

using namespace SST;
using namespace SST::n_Bank;
using namespace CramSim;

c_MemhBridgeContent::c_MemhBridgeContent(ComponentId_t x_id, Params& x_params) :
        c_MemhBridge(x_id, x_params) {

    /*---- LOAD PARAMS ----*/

    //used for reading params
    bool l_found = false;

    // internal params
    int verbosity = x_params.find<int>("verbose", 0);
    output = new SST::Output("CramSim.memhbridgeContent[@f:@l:@p] ",
                             verbosity, 0, SST::Output::STDOUT);

    /*---- CONFIGURE LINKS ----*/

    // Content links
    m_linkContent = configureLink( "linkContent",new Event::Handler<c_MemhBridgeContent>(this,&c_MemhBridgeContent::handleContentEvent) );

}

c_MemhBridgeContent::~c_MemhBridgeContent() {
}



void c_MemhBridgeContent::createTxn() {
    c_MemhBridge::createTxn();
/*
    uint64_t l_cycle = Simulation::getSimulation()->getCurrentSimCycle();

    SST::Event* e = 0;
    while((e = m_linkCPU->recv())) {
        MemReqEvent *event = dynamic_cast<MemReqEvent *>(e);

        c_Transaction *mTxn;
        ulong addr = event->getAddr();

        if (event->getIsWrite())
            mTxn = new c_Transaction(event->getReqId(), e_TransactionType::WRITE, addr, 1);
        else
            mTxn = new c_Transaction(event->getReqId(), e_TransactionType::READ, addr, 1);


        std::pair<c_Transaction*, uint64_t > l_entry = std::make_pair(mTxn,l_cycle);
        m_txnReqQ.push_back(l_entry);

        if(k_printTxnTrace)
            printTxn(event->getIsWrite(),addr);
    }
    */
}

void c_MemhBridgeContent::handleContentEvent(SST::Event *ev)
{
    fprintf(stderr,"test\n");
}

// Element Libarary / Serialization stuff
