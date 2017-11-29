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



#ifndef C_TRANSACTION_HPP
#define C_TRANSACTION_HPP

#include <ostream>
#include <iostream>
#include <map>
#include <string>
#include <list>
#include <memory>

//sst includes

#include <sst/core/component.h>
#include <sst/core/serialization/serializable.h>
#include <output.h>
//local includes
#include "c_HashedAddress.hpp"

typedef unsigned long ulong;

namespace SST {
namespace n_Bank {

class c_BankCommand;

enum class e_TransactionType { READ, WRITE };

class c_Transaction : public SST::Core::Serialization::serializable
{

private:
  uint64_t m_seqNum;
  e_TransactionType m_txnMnemonic;
  ulong m_addr;
    uint32_t m_compressedSize;
  //std::map<e_TransactionType,std::string> m_txnToString;
  
  bool m_isResponseReady;
  unsigned m_numWaitingCommands;
  unsigned m_dataWidth;
  bool m_processed; //<! flag that is set when this transaction is split into commands
  bool m_skip_metadata_lookup;
    bool m_metadataFlag;
    uint64_t m_hostReqId;

  //std::list<c_BankCommand*> m_cmdPtrList; //<! list of c_BankCommand shared_ptrs that compose this c_Transaction
  std::list<uint64_t> m_cmdSeqNumList; //<! list of c_BankCommand Sequence numbers that compose this c_Transaction
    c_HashedAddress m_hashedAddr;
    bool m_hasHashedAddr;

    int8_t m_compressed_size; //0,25,50,75,100
    c_Transaction* m_helper;
    bool m_helper_flag;
    bool m_isResponseRequired;
    double m_chipAccessRatio;

public:
    uint64_t m_time_arrived_Controller;
    uint64_t m_time_inserted_TxnQ;
    uint64_t m_time_inserted_CmdQ;
    uint64_t m_time_issued_CmdQ;
//  friend std::ostream& operator<< (std::ostream& x_stream, const c_Transaction& x_transaction);
    void setHostReqID(uint64_t reqid) {m_hostReqId=reqid;}
    uint64_t getHostReqID(){return m_hostReqId;}
  c_Transaction() {} // required for ImplementSerializable
  c_Transaction( ulong x_seqNum, e_TransactionType x_cmdType , ulong x_addr , unsigned x_dataWidth);
  ~c_Transaction();

  e_TransactionType getTransactionMnemonic() const;

  ulong getAddress() const;         //<! returns the address accessed by this command
  std::string getTransactionString() const; //<! returns the mnemonic of command
  void setMetaDataSkip(){
      m_skip_metadata_lookup=true;
  }
        void setMetaDataTxn() {m_metadataFlag=true;}
        bool isMetaDataSkip(){ return m_skip_metadata_lookup; }
        bool isMetaDataTxn(){ return m_metadataFlag; }
  void setResponseReady(); //<! sets the flag that this transaction has received its response.
  bool isResponseReady();  //<! returns the flag that this transaction has received its response.
        void donotRespond(){m_isResponseRequired=false;}
        bool isResponseRequired(){return m_isResponseRequired;}
        void setChipAccessRatio(double chipAccessRatio){m_chipAccessRatio=chipAccessRatio;}
        double getChipAccessRatio(){return m_chipAccessRatio;}
bool needHelper()
{
    if(m_compressed_size>50)
        return true;
    else
        return false;
}
        void setHelper(c_Transaction* helper){
            m_helper=helper;
        }

        void setHelperFlag(bool flag) {
            m_helper_flag = flag;
        }

        bool isHelper() {
            return m_helper_flag;
        }

        c_Transaction* getHelper(){
            return m_helper;
        }

  void setWaitingCommands(const unsigned x_numWaitingCommands);
  unsigned getWaitingCommands() const;

  bool matchesCmdSeqNum(ulong x_seqNum); //<! returns true if this transaction matches a command with x_seqNum
  void addCommandPtr(c_BankCommand* x_cmdPtr);

  ulong getSeqNum() const;

        void setCompressedSize(int8_t size){
            m_compressed_size=size;
        }

        int8_t getCompressedSize(){
            return m_compressed_size;
        }


  unsigned getDataWidth() const;
  unsigned getThreadId() const; // FIXME
  bool isProcessed() const;
  void isProcessed(bool x_processed);
  void print() const;
  void print(SST::Output *x_output, std::string x_prefix, SimTime_t x_cycle) const;


const c_HashedAddress& getHashedAddress() const{
         return (m_hashedAddr);
  }
  void setHashedAddress(const c_HashedAddress &x_hashedAddr) {
      m_hashedAddr = x_hashedAddr;
      m_hasHashedAddr=true;
  }
        bool hasHashedAddress(){
            return m_hasHashedAddr;
        }

        bool isRead(){
            return m_txnMnemonic==e_TransactionType ::READ;
        }

        bool isWrite(){
            return m_txnMnemonic==e_TransactionType ::WRITE;
        }


  void serialize_order(SST::Core::Serialization::serializer &ser) override ;
  
  ImplementSerializable(c_Transaction);
  
};

} // namespace n_Bank
} // namespace SST

#endif // C_TRANSACTION_HPP
