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


#ifndef _H_SST_PROSPERO_READER
#define _H_SST_PROSPERO_READER

#include <sst/core/component.h>
#include <sst/core/output.h>
#include <sst/core/subcomponent.h>
#include <sst/core/params.h>
#include <cstdint>
#include "atomichandler.h"


namespace SST {
namespace Prospero {

typedef enum {
	READ,
	WRITE
} ProsperoTraceEntryOperation;

class ProsperoTraceEntry {
public:
	ProsperoTraceEntry(
		const uint64_t eCyc,
		const uint64_t eAddr,
		const uint32_t eLen,
		const std::vector<uint64_t> eData,
		const uint64_t compRatio,
		const ProsperoTraceEntryOperation eOp,
		const uint32_t eAtom) :
		cycles(eCyc), address(eAddr), length(eLen), data_vector(eData), op(eOp), atomic(eAtom),data(0),compRatio(compRatio){

 //                   printf("address: 0x%x data: 0x%x\n",eAddr,eData);
		}

	ProsperoTraceEntry(
			const uint64_t eCyc,
			const uint64_t eAddr,
			const uint32_t eLen,
			const uint64_t eData,
			const ProsperoTraceEntryOperation eOp,
			const uint32_t eAtom) :
			cycles(eCyc), address(eAddr), length(eLen), data(eData), op(eOp), atomic(eAtom),compRatio(0){


		//                   printf("address: 0x%x data: 0x%x\n",eAddr,eData);
	}

	bool isRead() const { return op == READ;  }
	bool isWrite() const { return op == WRITE; }
	bool isAtomic() const { return atomic != NON_ATOMIC; }
	uint64_t getAddress() const { return address; }
	uint32_t getLength() const { return length; }
	uint32_t getData() const { return data; }
	uint64_t getIssueAtCycle() const { return cycles; }
	uint64_t getAtomic() const { return atomic; }
	uint64_t getInstNum() const {return instnum;}
	uint64_t getCycle() const {return cycles;}
	void setInstNum(uint64_t instnum_) {instnum=instnum_;}
	ProsperoTraceEntryOperation getOperationType() const { return op; }
	std::vector<uint64_t> getDataVector() const{return data_vector;}
private:
	const uint64_t cycles;
	const uint64_t address;
	const uint32_t length;
	const uint64_t data;
	std::vector<uint64_t> data_vector;
	const uint64_t compRatio;
	const uint32_t atomic;
	uint64_t instnum;
	const ProsperoTraceEntryOperation op;

};

class ProsperoTraceReader : public SubComponent {

public:
	ProsperoTraceReader( Component* owner, Params& params ) : SubComponent(owner) {};
	~ProsperoTraceReader() { };
	virtual ProsperoTraceEntry* readNextEntry() { return NULL; };
	void setOutput(Output* out) { output = out; }
	virtual void resetTrace(){};

protected:
	Output* output;
	uint64_t atomifyDrift;
	uint32_t pimSupport;
};

}
}

#endif
