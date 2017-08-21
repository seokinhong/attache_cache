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
#include "c_MemhBridge.hpp"
#include "c_TxnReqEvent.hpp"
#include "c_TxnResEvent.hpp"
#include "c_TokenChgEvent.hpp"
#include "memReqEvent.hpp"

using namespace SST;
using namespace SST::n_Bank;
using namespace CramSim;

c_MemhBridge::c_MemhBridge(ComponentId_t x_id, Params& x_params) :
	Component(x_id) {

	/*---- LOAD PARAMS ----*/

	//used for reading params
	bool l_found = false;

	// internal params
	m_seqNum = 0;
	m_resReadCount = 0;
	m_resWriteCount = 0;
	int verbosity = x_params.find<int>("verbose", 0);
	output = new SST::Output("CramSim.memhbridge[@f:@l:@p] ",
							 verbosity, 0, SST::Output::STDOUT);

	//internal queues' sizes
	k_txnGenReqQEntries = (uint32_t)x_params.find<uint32_t>("numTxnGenReqQEntries", 100,
			l_found);
	if (!l_found) {
		std::cout
				<< "TxnGen:: numTxnGenReqQEntries value is missing... exiting"
				<< std::endl;
		exit(-1);
	}

	k_txnGenResQEntries = (uint32_t)x_params.find<uint32_t>("numTxnGenResQEntries", 100,
															l_found);
	if (!l_found) {
		std::cout
				<< "TxnGen:: numTxnGenResQEntries value is missing... exiting"
				<< std::endl;
		exit(-1);
	}

	//transaction unit queue entries
	k_CtrlReqQEntries = (uint32_t)x_params.find<uint32_t>("numCtrlReqQEntries", 100,
			l_found);
	if (!l_found) {
		std::cout << "TxnGen:: numCtrlReqQEntries value is missing... exiting"
				<< std::endl;
		exit(-1);
	}
	m_txnUnitReqQTokens = k_CtrlReqQEntries;

	// set up txn trace output
	k_printTxnTrace = (uint32_t) x_params.find<uint32_t>("boolPrintTxnTrace", 0, l_found);

	k_txnTraceFileName = (std::string) x_params.find<std::string>("strTxnTraceFile", "-", l_found);
	//k_txnTraceFileName.pop_back(); // remove trailing newline (??)
	if (k_printTxnTrace) {
		if (k_txnTraceFileName.compare("-") == 0) {// set output to std::cout
			std::cout << "Setting txn trace output to std::cout" << std::endl;
			m_txnTraceStreamBuf = std::cout.rdbuf();
		} else { // open the file and direct the txnTraceStream to it
			std::cout << "Setting txn trace output to " << k_txnTraceFileName << std::endl;
			m_txnTraceOFStream.open(k_txnTraceFileName);
			if (m_txnTraceOFStream) {
				m_txnTraceStreamBuf = m_txnTraceOFStream.rdbuf();
			} else {
				std::cerr << "Failed to open txn trace output file " << k_txnTraceFileName << ", redirecting to stdout";
				m_txnTraceStreamBuf = std::cout.rdbuf();
			}
		}
		m_txnTraceStream = new std::ostream(m_txnTraceStreamBuf);
	}



	/*---- CONFIGURE LINKS ----*/

	// request-related links
	//// send to controller
	m_outTxnGenReqPtrLink = configureLink(
			"outTxnGenReqPtr",
			new Event::Handler<c_MemhBridge>(this,
					&c_MemhBridge::handleOutTxnGenReqPtrEvent));
	//// accept token chg from txn unit
	m_inTxnUnitReqQTokenChgLink = configureLink(
			"inCtrlReqQTokenChg",
			new Event::Handler<c_MemhBridge>(this,
					&c_MemhBridge::handleInTxnUnitReqQTokenChgEvent));

	// response-related links
	//// accept from controller
	m_inTxnUnitResPtrLink = configureLink(
			"inCtrlResPtr",
			new Event::Handler<c_MemhBridge>(this,
					&c_MemhBridge::handleInTxnUnitResPtrEvent));
	//// send token chg to controller
	m_outTxnGenResQTokenChgLink = configureLink(
			"outTxnGenResQTokenChg",
			new Event::Handler<c_MemhBridge>(this,
					&c_MemhBridge::handleOutTxnGenResQTokenChgEvent));

	// CPU links
	m_linkCPU = configureLink( "linkCPU");

	// get configured clock frequency
	std::string l_controllerClockFreqStr = (std::string)x_params.find<std::string>("strControllerClockFrequency", "1GHz", l_found);
	
	//set our clock
	registerClock(l_controllerClockFreqStr,
			new Clock::Handler<c_MemhBridge>(this, &c_MemhBridge::clockTic));
}

c_MemhBridge::~c_MemhBridge() {
}

c_MemhBridge::c_MemhBridge() :
	Component(-1) {
	// for serialization only
}


void c_MemhBridge::createTxn() {
	if (m_txnReqQ.size() < k_txnGenReqQEntries) {
		SST::Event* e = 0;
		while((e = m_linkCPU->recv())) {
			MemReqEvent *event = dynamic_cast<MemReqEvent *>(e);

			c_Transaction *mTxn;
			ulong addr = event->getAddr();

			if (event->getIsWrite())
				mTxn = new c_Transaction(event->getReqId(), e_TransactionType::WRITE, addr, 1);
			else
				mTxn = new c_Transaction(event->getReqId(), e_TransactionType::READ, addr, 1);

			m_txnReqQ.push(mTxn);
                        if(k_printTxnTrace)
                            printTxn(event->getIsWrite(),addr);
		}
	}
}

bool c_MemhBridge::clockTic(Cycle_t) {
	// std::cout << std::endl << std::endl << "TxnGen::clock tic" << std::endl;

	m_thisCycleResQTknChg = 0;

	// store the current number of entries in the queue, later compute the change
	m_thisCycleResQTknChg = m_txnResQ.size();


	createTxn();
	sendRequest();
	readResponse();

	m_thisCycleResQTknChg -= m_txnResQ.size();
	sendTokenChg();

	return false;

}

void c_MemhBridge::handleInTxnUnitReqQTokenChgEvent(SST::Event *ev) {
	c_TokenChgEvent* l_txnUnitReqQTknChgEventPtr =
			dynamic_cast<c_TokenChgEvent*> (ev);

	if (l_txnUnitReqQTknChgEventPtr) {
		// std::cout << "TxnGen::handleInTxnUnitReqQTokenChgEvent(): @"
		// 		<< std::dec
		// 		<< Simulation::getSimulation()->getCurrentSimCycle() << " "
		// 		<< __PRETTY_FUNCTION__ << std::endl;

		m_txnUnitReqQTokens += l_txnUnitReqQTknChgEventPtr->m_payload;

		//FIXME: Critical: This pointer is left dangling
		delete l_txnUnitReqQTknChgEventPtr;

		assert(m_txnUnitReqQTokens >= 0);
		assert(m_txnUnitReqQTokens <= k_CtrlReqQEntries);


	} else {
		std::cout << std::endl << std::endl << "TxnGen:: "
				<< __PRETTY_FUNCTION__ << " ERROR:: Bad event type!"
				<< std::endl;
	}
}

void c_MemhBridge::handleInTxnUnitResPtrEvent(SST::Event* ev) {
	// make sure the txn res q has at least one empty entry
	// to accept a new txn ptr
	assert(1 <= (k_txnGenResQEntries - m_txnResQ.size()));

	c_TxnResEvent* l_txnResEventPtr = dynamic_cast<c_TxnResEvent*> (ev);
	if (l_txnResEventPtr) {
	//	 std::cout << "TxnGen::handleInTxnUnitResPtrEvent(): @" << std::dec
	//	 		<< Simulation::getSimulation()->getCurrentSimCycle() << " "
	//	 		<< __PRETTY_FUNCTION__ << " Txn received: "<< std::endl;
	//	 l_txnResEventPtr->m_payload->print();
	//	 std::cout << std::endl;

		if (l_txnResEventPtr->m_payload->getTransactionMnemonic()
				== e_TransactionType::READ)
			m_resReadCount++;
		else
			m_resWriteCount++;


		m_txnResQ.push(l_txnResEventPtr->m_payload);

		//FIXME: Critical: This pointer is left dangling
		delete l_txnResEventPtr;
	} else {
		std::cout << std::endl << std::endl << "TxnGen:: "
				<< __PRETTY_FUNCTION__ << " ERROR:: Bad Event Type!"
				<< std::endl;
	}
}

// dummy event functions
void c_MemhBridge::handleOutTxnGenReqPtrEvent(SST::Event* ev) {
	// nothing to do here
	std::cout << __PRETTY_FUNCTION__ << " ERROR: Should not be here"
			<< std::endl;
}

void c_MemhBridge::handleOutTxnGenResQTokenChgEvent(SST::Event* ev) {
	// nothing to do here
	std::cout << __PRETTY_FUNCTION__ << " ERROR: Should not be here"
			<< std::endl;
}

void c_MemhBridge::sendTokenChg() {
	// only send tokens when space has opened up in queues
	// there are no negative tokens. token subtraction must be performed
	// in the source component immediately after sending an event
	if (m_thisCycleResQTknChg > 0) {

		//send res q token chg
		// std::cout << "TxnGen::sendTokenChg(): sending tokens: "
		// 		<< m_thisCycleResQTknChg << std::endl;
		c_TokenChgEvent* l_txnResQTokenChgEvPtr = new c_TokenChgEvent();
		l_txnResQTokenChgEvPtr->m_payload = m_thisCycleResQTknChg;
		m_outTxnGenResQTokenChgLink->send(l_txnResQTokenChgEvPtr);
	}
}

void c_MemhBridge::sendRequest() {

	if (m_txnUnitReqQTokens > 0) {
		if (m_txnReqQ.size() > 0) {


			m_txnReqQ.front()->print(output,"[memhbridge.sendRequest]");

			c_TxnReqEvent* l_txnReqEvPtr = new c_TxnReqEvent();
			l_txnReqEvPtr->m_payload = m_txnReqQ.front();
			m_txnReqQ.pop();
			m_outTxnGenReqPtrLink->send(l_txnReqEvPtr);
			--m_txnUnitReqQTokens;
		}
	}
}

void c_MemhBridge::readResponse() {
	if (m_txnResQ.size() > 0) {
		c_Transaction* l_txn = m_txnResQ.front();

		MemRespEvent *event = new MemRespEvent(l_txn->getSeqNum(), l_txn->getAddress(), 0);

		l_txn->print(output,"[memhbridge.readResponse]");

		m_linkCPU->send( event );
		m_txnResQ.pop();
	}
}

void c_MemhBridge::printTxn(bool x_isWrite, uint64_t x_addr){
    std::string l_txnType;
    
    if(x_isWrite)
        l_txnType="P_MEM_WR";
    else
        l_txnType="P_MEM_RD";

    (*m_txnTraceStream) << "0x" << std::hex <<x_addr
                        << " " <<l_txnType
                        << " " << "0"
                        <<std::endl;
		
}

// Element Libarary / Serialization stuff
