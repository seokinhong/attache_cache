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


#ifndef _H_SST_ARIEL_READ_EVENT
#define _H_SST_ARIEL_READ_EVENT

#include "arielevent.h"

using namespace SST;

namespace SST {
namespace ArielComponent {

class ArielReadEvent : public ArielEvent {

	public:
	    ArielReadEvent(uint64_t rAddr, uint32_t length, uint64_t *data) :
			readAddress(rAddr), readLength(length) {
               uint8_t* ptr=(uint8_t*)data;
			   uint64_t* ptr_64 = data;
               for(int i=0;i<8;i++) {
					//comp_debug
					//	fprintf(stderr, "[arielreadevent] addr:%llx read data: %x\n",rAddr, *ptr_64);
                   ptr_64++;
					for (int j = 0; j < 8; j++) {
						cacheLineData.push_back(*ptr);
						ptr++;
					}
				}
		}

		~ArielReadEvent() {

		}

		ArielEventType getEventType() const {
			return READ_ADDRESS;
		}

		uint64_t getAddress() const {
			return readAddress;
		}

		uint32_t getLength() const {
			return readLength;
		}

		std::vector<uint8_t> getData() const{
                        return cacheLineData;
		}

	private:
		const uint64_t readAddress;
		const uint32_t readLength;
	    std::vector<uint8_t> cacheLineData;

};

}
}

#endif
