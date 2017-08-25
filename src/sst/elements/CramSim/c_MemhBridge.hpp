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

#ifndef _MEMHBRIDGE_H
#define _MEMHBRIDGE_H

#include <stdint.h>
#include <queue>
#include <iostream>
#include <fstream>

//SST includes
#include <sst/core/component.h>
#include <sst/core/link.h>
#include <sst/core/output.h>

//local includes
#include "c_Transaction.hpp"
#include "c_TxnGen.hpp"

namespace SST {
namespace n_Bank {
class c_MemhBridge: public c_TxnGenBase {


public:
	c_MemhBridge(SST::ComponentId_t x_id, SST::Params& x_params);
	~c_MemhBridge();


private:
	void createTxn();
	void readResponse(); //read from res q to output

        void printTxn(bool isWrite, uint64_t addr);
	
        //Debug
	Output *output;


	//link to/from CPU
	SST::Link *m_linkCPU;


	bool k_printTxnTrace;
	std::string k_txnTraceFileName;
	std::filebuf m_txnTraceFileBuf;
	std::streambuf *m_txnTraceStreamBuf;
	std::ofstream m_txnTraceOFStream;
	std::ostream *m_txnTraceStream;


};

} // namespace n_Bank
} // namespace SST

#endif  /* _TXNGENRAND_H */
