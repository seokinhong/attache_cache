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


#include "sst_config.h"
#include "prosbinaryreader.h"

using namespace SST::Prospero;

ProsperoBinaryTraceReader::ProsperoBinaryTraceReader( Component* owner, Params& params ) :
	ProsperoTraceReader(owner, params) {

		std::string traceFile = params.find<std::string>("file", "");
		traceName = traceFile;	
		traceInput = fopen(traceFile.c_str(), "rb");

		if(NULL == traceInput) {
			fprintf(stderr, "Fatal: Error opening trace file: %s in binary reader.\n",
					traceFile.c_str());
			exit(-1);
		}	

		pimSupport = (uint32_t) params.find<uint32_t>("pimsupport", 0);
		atomifyDrift = 0;

		recordLength = sizeof(uint64_t) + sizeof(char) + sizeof(uint64_t) + sizeof(uint32_t) + + sizeof(uint32_t);
		buffer = (char*) malloc(sizeof(char) * recordLength);
};

ProsperoBinaryTraceReader::~ProsperoBinaryTraceReader() {
	if(NULL != traceInput) {
		fclose(traceInput);
	}

	if(NULL != buffer) {
		free(buffer);
	}
}

void ProsperoBinaryTraceReader::copy(char* target, const char* source,
		const size_t bufferOffset, const size_t len) {

	for(size_t i = 0; i < len; ++i) {
		target[i] = source[bufferOffset + i];
	}
}

ProsperoTraceEntry* ProsperoBinaryTraceReader::readNextEntry() {
	uint64_t reqAddress = 0;
	uint64_t reqCycles  = 0;
	char reqType = 'R';
	uint32_t reqLength  = 0;
	uint32_t reqAtomic  = NON_ATOMIC;
	uint64_t atomifyStart = 0;
	uint64_t atomifyBefore = 0;
	int64_t atomifyRange = 0;
	fpos_t pos;

	if(feof(traceInput)) {
		return NULL;
	}

	if(1 == fread(buffer, (size_t) recordLength, (size_t) 1, traceInput)) {
		// We DID read an entry
		copy((char*) &reqCycles,  buffer, (size_t) 0, sizeof(uint64_t));
		copy((char*) &reqType,    buffer, sizeof(uint64_t), sizeof(char));
		copy((char*) &reqAddress, buffer, sizeof(uint64_t) + sizeof(char), sizeof(uint64_t));
		copy((char*) &reqLength,  buffer, sizeof(uint64_t) + sizeof(char) + sizeof(uint64_t), sizeof(uint32_t));
		copy((char*) &reqAtomic,  buffer, sizeof(uint64_t) + sizeof(char) + sizeof(uint64_t) + sizeof(uint32_t), sizeof(uint32_t));
	
		fgetpos (traceInput,&pos);
		output->verbose(CALL_INFO, 2, 0, "%s: Request cycle trace %lu, pos : %lu\n", traceName.c_str(), reqCycles, pos);

		if (pimSupport > 1){	
			// We want to remove any ATOMIFIED instrusctions and only issue the true Atomics
			// When we find an ATOMOFIED instruction we mark the cycle of the request
			// Then we go over the ATOMIFIED instructions until we either find an Atomic or Atomified instructions stop
			
			if (reqAtomic == ATOMIC_ATOMIFY){
				atomifyStart = reqCycles;
				output->verbose(CALL_INFO, 2, 0, "%s: Atomify starts at cycle : %lu,  pos : %lu\n", traceName.c_str(), atomifyStart, pos);
			}
			
			while (reqAtomic == ATOMIC_ATOMIFY){
				if(1 == fread(buffer, (size_t) recordLength, (size_t) 1, traceInput)) {
					// We DID read an entry
					fgetpos (traceInput,&pos);
					copy((char*) &reqCycles,  buffer, (size_t) 0, sizeof(uint64_t));
					copy((char*) &reqType,    buffer, sizeof(uint64_t), sizeof(char));
					copy((char*) &reqAddress, buffer, sizeof(uint64_t) + sizeof(char), sizeof(uint64_t));
					copy((char*) &reqLength,  buffer, sizeof(uint64_t) + sizeof(char) + sizeof(uint64_t), sizeof(uint32_t));
					copy((char*) &reqAtomic,  buffer, sizeof(uint64_t) + sizeof(char) + sizeof(uint64_t) + sizeof(uint32_t), sizeof(uint32_t));
					atomifyBefore = reqCycles;
					output->verbose(CALL_INFO, 2, 0, "%s Atomify before cycle %lu, pos: %lu \n", traceName.c_str(), atomifyBefore, pos);
				}
				else{
					return NULL;
				}
			}
			
			// At this point the instruction is valid but the cycles may be skewed
			// We adjust the cycles based on the local and global drift counters
			atomifyRange = atomifyBefore - atomifyStart;
			
			if (atomifyRange < 0){
				output->verbose(CALL_INFO, 1, 0, "%s: -- AtomifyStart: %lu, AtomifyBefore: %lu, AtomifyRange %ld \n", traceName.c_str(), atomifyStart, atomifyBefore, atomifyRange);
				output->verbose(CALL_INFO, 1, 0, "%s: -- ReqCycles before %lu, pos : %lu\n",  traceName.c_str(), reqCycles, pos);
				output->fatal(CALL_INFO, -1, "%s Future instruction has smaller Instruction count than older instruction!!!\n", traceName.c_str());
				exit(-1);
			}

			if(atomifyStart > 0){
				output->verbose(CALL_INFO, 2, 0, "%s: AtomifyStart: %lu, AtomifyBefore: %lu, AtomifyDrift %lu \n", traceName.c_str(), atomifyStart, atomifyBefore, atomifyDrift);
				output->verbose(CALL_INFO, 2, 0, "%s: ReqCycles before %lu \n",  traceName.c_str(), reqCycles);
			}
			
			atomifyDrift = atomifyDrift + atomifyBefore - atomifyStart;
			output->verbose(CALL_INFO, 2, 0, "%s: AtomifyDrift after %lu \n", traceName.c_str(), atomifyDrift);

			reqCycles = reqCycles - atomifyDrift; 
						
			output->verbose(CALL_INFO, 2, 0, "%s: Request cycle actual %lu \n",  traceName.c_str(), reqCycles);
		}

		return new ProsperoTraceEntry(reqCycles, reqAddress,
				reqLength,
				(reqType == 'R' || reqType == 'r') ? READ : WRITE, reqAtomic);
	} else {
		// Did not get a full read?
		return NULL;
	}
}
