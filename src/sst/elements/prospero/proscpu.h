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

#ifndef _SST_PROSPERO_H
#define _SST_PROSPERO_H

#include "sst/core/output.h"
#include "sst/core/component.h"
#include "sst/core/element.h"
#include "sst/core/params.h"
#include "sst/core/event.h"
#include "sst/core/sst_types.h"
#include "sst/core/component.h"
#include "sst/core/link.h"
#include "sst/core/interfaces/simpleMem.h"

#include "prosreader.h"
#include "prosmemmgr.h"
#include "atomichandler.h"


#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

using namespace SST;
using namespace SST::Interfaces;

namespace SST {
	namespace Prospero {

		class ProsperoComponent : public Component {
			public:

				ProsperoComponent(ComponentId_t id, Params& params);
				~ProsperoComponent();

				void setup() { }
				void finish();

			private:
				ProsperoComponent();                         // Serialization only
				ProsperoComponent(const ProsperoComponent&); // Do not impl.
				void operator=(const ProsperoComponent&);    // Do not impl.

				void handleResponse( SimpleMem::Request* ev );
				bool tick( Cycle_t );
				void issueRequest(const ProsperoTraceEntry* entry);
				void countAtomicInstr(uint32_t opcode);


				Output* output;
				ProsperoTraceReader* reader;
				ProsperoTraceEntry* currentEntry;
				ProsperoMemoryManager* memMgr;
				SimpleMem* cache_link;
				char* subID;
				FILE* traceFile;
				bool traceEnded;
#ifdef HAVE_LIBZ
				gzFile traceFileZ;
#endif
				//atomic instructions
				uint32_t pimSupport;
				uint32_t profileAtomics;
				uint32_t waitForCycle;

				uint64_t pageSize;
				uint64_t cacheLineSize;
				uint64_t maxOutstanding;
				uint64_t currentOutstanding;
				uint64_t currentOutstandingUC;
				uint64_t maxIssuePerCycle;
				uint64_t issuedAtomic;

				uint64_t readsIssued;
				uint64_t writesIssued;
				uint64_t splitReadsIssued;
				uint64_t splitWritesIssued;
				uint64_t totalBytesRead;
				uint64_t totalBytesWritten;
				uint64_t cyclesWithIssue;
				uint64_t cyclesWithNoIssue;
				uint64_t cyclesNoInstr;
				uint64_t cyclesDrift;

				Statistic<uint64_t>* statReadRequests;
				Statistic<uint64_t>* statWriteRequests;
				Statistic<uint64_t>* statSplitReadRequests;
				Statistic<uint64_t>* statSplitWriteRequests;
				Statistic<uint64_t>* statNoopCount;
				Statistic<uint64_t>* statInstructionCount;
				Statistic<uint64_t>* statCyclesIssue;
				Statistic<uint64_t>* statCyclesNoIssue;
				Statistic<uint64_t>* statCyclesNoInstr;
				Statistic<uint64_t>* statCycles;
				Statistic<uint64_t>* statBytesRead;
				Statistic<uint64_t>* statBytesWritten;

				// atomic instructions
				Statistic<uint64_t>* statAtomicInstrCount;
				Statistic<uint64_t>* statA_AddIns;
				Statistic<uint64_t>* statA_AdcIns;
				Statistic<uint64_t>* statA_AndIns;
				Statistic<uint64_t>* statA_BtcIns;
				Statistic<uint64_t>* statA_BtrIns;
				Statistic<uint64_t>* statA_BtsIns;
				Statistic<uint64_t>* statA_XchgIns;
				Statistic<uint64_t>* statA_CMPXchgIns;
				Statistic<uint64_t>* statA_DecIns;
				Statistic<uint64_t>* statA_IncIns;
				Statistic<uint64_t>* statA_NegIns;
				Statistic<uint64_t>* statA_NotIns;
				Statistic<uint64_t>* statA_OrIns;
				Statistic<uint64_t>* statA_SbbIns;
				Statistic<uint64_t>* statA_SubIns;
				Statistic<uint64_t>* statA_XorIns;
				Statistic<uint64_t>* statA_XaddIns;
				Statistic<uint64_t>* statA_Atomify;	
		};

	}
}

#endif /* _SST_PROSPERO_H */
