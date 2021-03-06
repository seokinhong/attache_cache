
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

// Copyright 2016 IBM Corporation

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

#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <map>
#include <assert.h>

#include "c_CmdScheduler.hpp"
#include "c_DeviceDriver.hpp"

using namespace SST;
using namespace SST::n_Bank;

c_CmdScheduler::c_CmdScheduler(Component *comp, Params &x_params) : SubComponent(comp){
    m_owner = dynamic_cast<c_Controller*>(comp);
    m_deviceController=m_owner->getDeviceDriver();
    output=dynamic_cast<c_Controller*>(comp)->getOutput();
    //create command queue
    m_numBanks=m_owner->getDeviceDriver()->getTotalNumBank();
    m_numChannels=m_owner->getDeviceDriver()->getNumChannel();
    m_numRanksPerChannel=m_owner->getDeviceDriver()->getNumRanksPerChannel();
    m_numBanksPerChannel=m_numBanks/m_numChannels;
    m_numBanksPerRank = m_numBanks/m_numRanksPerChannel;

    assert(m_numBanks>0);
    m_cmdQueues.clear();

    m_cmdQueues.resize(m_numChannels);
    m_nextCmdQIdx.resize(m_numChannels);
    for(unsigned l_ch=0;l_ch<m_numChannels;l_ch++) {
        m_nextCmdQIdx.at(l_ch)=0;
        for (unsigned l_bankIdx = 0; l_bankIdx < m_numBanks; l_bankIdx++) {
            m_cmdQueues.at(l_ch).resize(m_numBanksPerChannel);
        }
    }

    bool l_found = false;
    k_numCmdQEntries = (uint32_t) x_params.find<uint32_t>("numCmdQEntries", 32, l_found);
    if (!l_found) {
        std::cout << "numCmdQEntries value is missing... it will be 32 (default)" << std::endl;
    }

    std::string l_cmdSchedulingPolicy = (std::string) x_params.find<std::string>("cmdSchedulingPolicy", "BANK", l_found);
    if (!l_found) {
        std::cout << "cmdScheduligPolicy is missing... it will be \"bank round robin\" (default)" << std::endl;
    }
    if(l_cmdSchedulingPolicy=="BANK")
        m_schedulingPolicy = e_SchedulingPolicy::BANK;  //Bank Round Robin
    else if(l_cmdSchedulingPolicy=="RANK")
        m_schedulingPolicy = e_SchedulingPolicy::RANK;  //Rank Round Robin
    else
    {
        std::cout << "CmdScheduler: scheduling policy error!\n";
        exit(-1);
    }
}


c_CmdScheduler::~c_CmdScheduler(){
}


uint64_t totalQueueing_delay_acc=0;
void c_CmdScheduler::run(){

    bool isSuccess = false;
    c_BankCommand *l_cmdPtr= nullptr;
    SimTime_t  l_time=m_owner->getSimCycle();

    for(unsigned l_ch=0;l_ch<m_numChannels;l_ch++) {

        unsigned nextBankIdx = m_nextCmdQIdx.at(l_ch);
        for (unsigned i = 0; i < m_numBanksPerChannel; i++) {
            c_CmdQueue &l_cmdQueue = m_cmdQueues[l_ch].at(nextBankIdx);

            if (!l_cmdQueue.empty()) {
                l_cmdPtr = l_cmdQueue.front();

                if (m_deviceController->isCmdAllowed(l_cmdPtr)) {
                    isSuccess = m_deviceController->push(l_cmdPtr);
                    if (isSuccess) {
                        l_cmdQueue.pop_front();
                        if(!l_cmdPtr->isHelper() && !l_cmdPtr->isMetadataCmd() && (l_cmdPtr->getCommandMnemonic()==e_BankCommandType::READ || l_cmdPtr->getCommandMnemonic()==e_BankCommandType::WRITE))
                        {

                            l_cmdPtr->getTransaction()->m_time_issued_CmdQ=m_owner->getSimCycle();
                            uint64_t totalQueueing_delay = m_owner->getSimCycle() - l_cmdPtr->getTransaction()->m_time_inserted_TxnQ;
                            totalQueueing_delay_acc+=totalQueueing_delay;
                            //printf("cmd issued, txn_id: %lld txnq_inserted_time: %lld queueing delay_acc:%lld\n",
                            //       l_cmdPtr->getTransaction()->getSeqNum(),l_cmdPtr->getTransaction()->m_time_inserted_TxnQ,totalQueueing_delay_acc);
                            uint64_t cmdQueueing_delay = m_owner->getSimCycle() - l_cmdPtr->getTransaction()->m_time_inserted_CmdQ;
                            uint64_t txnQueueing_delay = l_cmdPtr->getTransaction()->m_time_inserted_CmdQ - l_cmdPtr->getTransaction()->m_time_inserted_TxnQ;
                            m_owner->s_totalQueueing_delay->addData(totalQueueing_delay);
                            m_owner->s_cmdQueueing_delay->addData(cmdQueueing_delay);
                            m_owner->s_txnQueueing_delay->addData(txnQueueing_delay);
                        }

//#ifdef __SST_DEBUG_OUTPUT__
                        l_cmdPtr->print(output, "[c_CmdScheduler]",m_owner->getSimCycle());
//#endif
                    }
                }
            }

            if(m_schedulingPolicy==e_SchedulingPolicy::BANK)
                nextBankIdx = (nextBankIdx + 1) % m_numBanksPerChannel;
            else if(m_schedulingPolicy==e_SchedulingPolicy::RANK)
                nextBankIdx=(nextBankIdx+m_numBanksPerRank)%(m_numBanksPerChannel-1);

        }
        if(m_schedulingPolicy==e_SchedulingPolicy::BANK)
            m_nextCmdQIdx.at(l_ch)=(m_nextCmdQIdx.at(l_ch)+1)%m_numBanksPerChannel;
        else if(m_schedulingPolicy==e_SchedulingPolicy::RANK)
            m_nextCmdQIdx.at(l_ch)=(m_nextCmdQIdx.at(l_ch)+m_numBanksPerRank)%(m_numBanksPerChannel-1);
    }
}


bool c_CmdScheduler::push(c_BankCommand* x_cmd) {
    unsigned l_ch=x_cmd->getHashedAddress()->getChannel();
    unsigned l_bank=x_cmd->getHashedAddress()->getBankId() % m_numBanksPerChannel;

    if (m_cmdQueues[l_ch].at(l_bank).size() < k_numCmdQEntries) {
        m_cmdQueues[l_ch].at(l_bank).push_back(x_cmd);
        return true;
    } else
        return false;

}


unsigned c_CmdScheduler::getToken(const c_HashedAddress &x_addr)
{
    unsigned l_ch=x_addr.getChannel();
    unsigned l_bank=x_addr.getBankId() % m_numBanksPerChannel;

    return k_numCmdQEntries-m_cmdQueues[l_ch].at(l_bank).size();

}
