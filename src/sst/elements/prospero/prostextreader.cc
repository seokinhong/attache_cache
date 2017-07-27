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
#include "prostextreader.h"

using namespace SST::Prospero;

ProsperoTextTraceReader::ProsperoTextTraceReader( Component* owner, Params& params ) :
	ProsperoTraceReader(owner, params) {

	std::string traceFile = params.find<std::string>("file", "");
	traceInput = fopen(traceFile.c_str(), "rt");
	traceName = traceFile;	

	if(NULL == traceInput) {
		fprintf(stderr, "Fatal: Unable to open file: %s in text reader.\n", traceFile.c_str());
		exit(-1);
	}
	pimSupport = (uint32_t) params.find<uint32_t>("pimsupport", 0);

	atomifyDrift = 0;

};

ProsperoTextTraceReader::~ProsperoTextTraceReader() {
	if(NULL != traceInput) {
		fclose(traceInput);
	}
}

ProsperoTraceEntry* ProsperoTextTraceReader::readNextEntry() {
	uint64_t reqAddress = 0;
	uint64_t reqCycles  = 0;
	char reqType = 'R';
	uint32_t reqLength  = 0;
	uint32_t reqAtomic = NON_ATOMIC;
	uint64_t atomifyStart = 0;
	uint64_t atomifyBefore = 0;
	int64_t atomifyRange = 0;


	if(EOF == fscanf(traceInput, "%" PRIu64 " %c %" PRIu64 " %" PRIu32 " %" PRIu32"",
				&reqCycles, &reqType, &reqAddress, &reqLength, &reqAtomic) ) {
		return NULL;
	} else {
		if (pimSupport > 1){	
			// We want to compress any ATOMIFIED instrusctions and only issue the true Atomics
			// When we find an ATOMOFIED instruction we mark the cycle of the request
			// Then we go over the ATOMIFIED instructions until we either find an Atomic or Atomified instructions stop
			
			if (reqAtomic == ATOMIC_ATOMIFY){
				atomifyStart = reqCycles;
				output->verbose(CALL_INFO, 2, 0, "%s: Atomify starts at cycle %lu \n", traceName.c_str(), atomifyStart);
			}

			while (reqAtomic == ATOMIC_ATOMIFY){
				if(EOF != fscanf(traceInput, "%" PRIu64 " %c %" PRIu64 " %" PRIu32 " %" PRIu32"", &reqCycles, &reqType, &reqAddress, &reqLength, &reqAtomic)) {
					atomifyBefore = reqCycles;
					output->verbose(CALL_INFO, 2, 0, "%s Atomify before cycle %lu \n", traceName.c_str(), atomifyBefore);
				}
				else{
					return NULL;
				}
			}	

			// At this point the instruction is valid but the cycles may be skewed
			// We adjust the cycles based on the local and global drift counters
			atomifyRange = atomifyBefore - atomifyStart;
			if (atomifyRange < 0){
				output->verbose(CALL_INFO, 0, 0, "%s: AtomifyStart: %lu, AtomifyBefore: %lu, AtomifyRange %ld \n", traceName.c_str(), atomifyStart, atomifyBefore, atomifyRange);
				output->verbose(CALL_INFO, 0, 0, "%s: ReqCycles before %lu \n",  traceName.c_str(), reqCycles);
				output->fatal(CALL_INFO, -1, "Future instruction has smaller Instruction count than older instruction!!!\n");
			}

			if(atomifyStart > 0){
				output->verbose(CALL_INFO, 2, 0, "%s: AtomifyStart: %lu, AtomifyBefore: %lu, AtomifyDrift %lu \n", traceName.c_str(), atomifyStart, atomifyBefore, atomifyDrift);
				output->verbose(CALL_INFO, 2, 0, "%s: ReqCycles before %lu \n",  traceName.c_str(), reqCycles);
			}
			
			atomifyDrift = atomifyDrift + atomifyBefore - atomifyStart;
			reqCycles = reqCycles - atomifyDrift; 
						
			output->verbose(CALL_INFO, 2, 0, "%s: Request cycle actual %lu \n",  traceName.c_str(), reqCycles);
		}
		
		return new ProsperoTraceEntry(reqCycles, reqAddress,
				reqLength,
				(reqType == 'R' || reqType == 'r') ? READ : WRITE, reqAtomic);
	}
}
