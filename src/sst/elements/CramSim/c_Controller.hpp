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

#ifndef C_CONTROLLER_HPP
#define C_CONTROLLER_HPP


// SST includes
#include <sst/core/link.h>
#include <sst/core/component.h>
#include "c_AddressHasher.hpp"
#include "c_DeviceDriver.hpp"
#include "c_TxnConverter.hpp"
#include "c_CmdScheduler.hpp"
#include "c_TxnScheduler.hpp"


// local includes
namespace SST {
    namespace n_Bank {
        class c_AddressHasher;
        class c_TxnScheduler;
        class c_DeviceDriver;
        class c_TxnConverter;
        class c_CmdScheduler;

        class c_Controller : public SST::Component {

        public:
            c_Controller(SST::ComponentId_t id, SST::Params &params);
            ~c_Controller();

            c_TxnScheduler* getTxnScheduler() {return m_txnScheduler;}
            c_AddressHasher* getAddrHasher() {return m_addrHasher;}
            c_DeviceDriver* getDeviceDriver() {return m_deviceDriver;}
            c_CmdScheduler* getCmdScheduler() {return m_cmdScheduler;}
            c_TxnConverter* getTxnConverter() {return m_txnConverter;}
            Output * getOutput() {return output;}

            void sendCommand(c_BankCommand* cmd);

            SimTime_t getSimCycle(){return m_simCycle;}

        protected:
            c_Controller(); // for serialization only
            c_Controller(SST::ComponentId_t id);

            virtual bool clockTic(SST::Cycle_t); // called every cycle


            void sendResponse();
            void sendRequest();
            void configure_link();
            // Transaction Generator <-> Controller Handlers
            void handleIncomingTransaction(SST::Event *ev);

            // Controller <--> memory devices
            void handleInDeviceResPtrEvent(SST::Event *ev);

            SimTime_t m_simCycle;

            SST::Output *output;

            std::deque<c_Transaction*> m_ReqQ;
            std::deque<c_Transaction*> m_MReqQ; //request Queue for meta data
            std::deque<c_Transaction*> m_ResQ;
            std::map<uint64_t,c_Transaction*> m_helperTxnQ;

            // Subcomponents
            c_TxnScheduler *m_txnScheduler;
            c_TxnConverter *m_txnConverter;
            c_CmdScheduler *m_cmdScheduler;
            c_AddressHasher *m_addrHasher;
            c_DeviceDriver *m_deviceDriver;

            // params for system configuration
            int k_enableQuickResponse;

		    // clock frequency
			std::string k_controllerClockFreqStr;

            // Transaction Generator <-> Controller Links
            SST::Link *m_txngenLink;
            // Controller <-> Memory device Links
            SST::Link *m_memLink;
        };
    }
}


#endif //C_CONTROLLER_HPP
