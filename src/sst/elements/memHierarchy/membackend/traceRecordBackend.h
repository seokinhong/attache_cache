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


#ifndef _H_SST_MEMH_TRACERECORD_BACKEND
#define _H_SST_MEMH_TRACERECORD_BACKEND

#include "membackend/memBackend.h"
#include <zlib.h>

namespace SST {
namespace MemHierarchy {

class traceRecordBackend : public SimpleMemBackend {
public:
	traceRecordBackend(Component *comp, Params &params);
	~traceRecordBackend();
    virtual bool issueRequest( ReqId, Addr, bool isWrite, unsigned numBytes );
	bool issueRequest( ReqId, Addr, bool isWrite ,unsigned numDATA, std::vector<uint64_t> data);

	virtual bool isClocked() { return false; }
	virtual bool clock(Cycle_t cycle);

private:
	std::vector<ReqId> memReqs;
	std::map<uint32_t, uint64_t> m_inst_num;   //global instruction count

	void recordTrace(uint64_t x_thread_id, bool x_isWrite, uint64_t x_addr,uint64_t x_inst_num,uint64_t compratio_bdi);
	// std::map<uint64_t, c_Cacheline*> backing_;
	// std::map<uint64_t, uint32_t> compratio_bdi;
	// std::map<uint64_t, uint32_t> compratio_fpc;
	// std::map<uint64_t, uint32_t> compratio_fvc;

    int verbosity;

   // int m_numThread;
    bool k_traceDebug;			//enable to print a text-mode trace file
  //  bool k_loopback_mode;			//enable the loopback mode
   // bool boolStoreContent;
   // bool boolStoreCompRate;
   // std::map<COMP_ALG, uint64_t> m_sumCompRate;
   // uint64_t m_cntReqs;
    std::vector<gzFile> traceZ;

    // Statistics
    std::map<int, uint64_t> m_normalized_size;


	int m_maxNumOutstandingReqs;
};

}
}

#endif
