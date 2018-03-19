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
#include "prosbingzreader.h"

using namespace SST::Prospero;

ProsperoCompressedBinaryTraceReader::ProsperoCompressedBinaryTraceReader( Component* owner, Params& params ) :
	ProsperoTraceReader(owner, params) {

	std::string traceFile = params.find<std::string>("file", "");
	traceInput = gzopen(traceFile.c_str(), "rb");
	hasContent = (bool)params.find<bool>("content", false);
	hasCompRatio = (bool)params.find<bool>("compression", false);
	hasAtomic = (bool)params.find<bool>("atomic",false);
	hasInstNum = (bool)params.find<bool>("instNum",false);

	if(Z_NULL == traceInput) {
		fprintf(stderr, "Fatal: attempted to open: %s but zlib returns error condition.\n",
			traceFile.c_str());
		exit(-1);
	}

	recordLength = sizeof(uint64_t) + sizeof(char) + sizeof(uint64_t) + sizeof(uint32_t);

	if(hasContent)
		recordLength+=sizeof(uint64_t)*8;
	if(hasCompRatio)
		recordLength+=sizeof(uint8_t);
	if(hasAtomic)
		recordLength+=sizeof(uint32_t);
	if(hasInstNum)
		recordLength+=sizeof(uint64_t);



	buffer = (char*) malloc(sizeof(char) * recordLength);
	std::cout<<recordLength<<std::endl;
};

ProsperoCompressedBinaryTraceReader::~ProsperoCompressedBinaryTraceReader() {
	if(NULL != traceInput) {
		gzclose(traceInput);
	}

	if(NULL != buffer) {
		free(buffer);
	}
}

void ProsperoCompressedBinaryTraceReader::copy(char* target, const char* source,
	const size_t bufferOffset, const size_t len) {

	for(size_t i = 0; i < len; ++i) {
		target[i] = source[bufferOffset + i];
	}
}

void ProsperoCompressedBinaryTraceReader::resetTrace()
{
	gzrewind(traceInput);
}



ProsperoTraceEntry* ProsperoCompressedBinaryTraceReader::readNextEntry() {
	output->verbose(CALL_INFO, 4, 0, "Reading next trace entry...\n");

	uint64_t reqAddress = 0;
	uint64_t reqCycles  = 0;
	char reqType = 'R';
	uint32_t reqLength  = 0;
	std::vector<uint64_t> reqData_vector;
	std::vector<uint8_t> compRatio_vector;
	uint32_t reqAtomic = NON_ATOMIC;
	uint64_t instNum = 0;

	if(gzeof(traceInput)) {
		output->verbose(CALL_INFO, 2, 0, "End of trace file reached, returning empty request.\n");
		return NULL;
	}

	unsigned int bytesRead = gzread(traceInput, buffer, (unsigned int) recordLength);
	if(bytesRead == recordLength) {
		uint64_t accumulate_length=0;

		// We DID read an entry
		copy((char*) &reqCycles,  buffer, (size_t) 0, sizeof(uint64_t));
		accumulate_length=sizeof(uint64_t);
		if(hasInstNum) {
			copy((char *) &instNum, buffer, accumulate_length, sizeof(uint64_t));
			accumulate_length+=sizeof(uint64_t);
		} else{
			instNum=reqCycles;
		}

		copy((char*) &reqType,    buffer, accumulate_length, sizeof(char));
		accumulate_length+= sizeof(char);
		copy((char*) &reqAddress, buffer, accumulate_length, sizeof(uint64_t));
		accumulate_length+=  sizeof(uint64_t);
		copy((char*) &reqLength,  buffer, accumulate_length,sizeof(uint32_t));
		accumulate_length+= sizeof(uint32_t);


		if(hasContent) {
			uint64_t reqData[8];
			copy((char *) reqData, buffer, accumulate_length, sizeof(uint64_t)*8);
			accumulate_length+=sizeof(uint64_t)*8;

			for(int i=0;i<8;i++)
			{
				reqData_vector.push_back(reqData[i]);
			}
		}

		if(hasCompRatio) {
			uint8_t compRatio;
			copy((char *) &compRatio, buffer, accumulate_length, sizeof(uint8_t));
			compRatio_vector.push_back(compRatio);
			accumulate_length+=sizeof(uint8_t);
		}


		if(hasAtomic) {
			copy((char *) &reqAtomic, buffer, accumulate_length, sizeof(uint32_t));
			accumulate_length+=sizeof(uint32_t);
		}

		ProsperoTraceEntry *new_entry=new ProsperoTraceEntry(reqCycles, reqAddress,
														 reqLength,reqData_vector,compRatio_vector,
														 (reqType == 'R' || reqType == 'r') ? READ : WRITE, reqAtomic);

		new_entry->setInstNum(instNum);

/*
		std::cout << std::dec<<reqCycles
				  << " " << instNum
				  << " "<< reqType
				  << " " <<std::hex<<reqAddress
				  << " "<<std::dec <<(int)compRatio<< std::endl;
*/
		return new_entry;

	} else {
		output->verbose(CALL_INFO, 2, 0, "Did not read a full record from the compressed trace, returning empty request.\n");
		// Did not get a full read?
		return NULL;
	}
}
