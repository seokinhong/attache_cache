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

#include <sst_config.h>

#define SST_ELI_COMPILE_OLD_ELI_WITHOUT_DEPRECATION_WARNINGS

#include "sst/core/element.h"
#include "sst/core/component.h"

#include "proscpu.h"
#include "prostextreader.h"
#include "prosbinaryreader.h"
#ifdef HAVE_LIBZ
#include "prosbingzreader.h"
#endif

using namespace SST;
using namespace SST::Prospero;

static Component* create_ProsperoCPU(ComponentId_t id, Params& params) {
	return new ProsperoComponent( id, params );
}

static SubComponent* create_TextTraceReader(Component* comp, Params& params) {
	return new ProsperoTextTraceReader(comp, params);
}

static SubComponent* create_BinaryTraceReader(Component* comp, Params& params) {
	return new ProsperoBinaryTraceReader(comp, params);
}

#ifdef HAVE_LIBZ
static SubComponent* create_CompressedBinaryTraceReader(Component* comp, Params& params) {
	return new ProsperoCompressedBinaryTraceReader(comp, params);
}
#endif

static const ElementInfoStatistic prospero_statistics[] = {
	{ "read_requests",        "Stat read_requests", "requests", 1},   // Name, Desc, Enable Level
	{ "write_requests",       "Stat write_requests", "requests", 1},
	{ "split_read_requests",  "Stat split_read_requests", "requests", 1},
	{ "split_write_requests", "Stat split_write_requests", "requests", 1},
	{ "cycles_issue", "Stat cycles with an issued memory request", "requests", 1},
	{ "cycles_no_issue", "Stat cycles without an issued memory request due to LS queue being full", "requests", 1},
	{ "cycles_tlb_misses", "Stat cycles without an issued memory request due to TLB misses", "requests", 1},
	{ "cycles_no_instr", "Stat cycles without an issued memory request due to instruction being far ahead in time", "requests", 1},
	{ "cycles",       "Stat simulation cycles", "requests", 1},
	{ "cycles_lsq_full",       "Stat cycles for lsq full", "requests", 1},
	{ "cycles_rob_full",       "Stat cycles for rob full", "requests", 1},
	{ "bytes_read",       "Stat bytes read", "requests", 1},
	{ "bytes_written",       "Stat bytes written", "requests", 1},
	{ "instruction_count",    "Statistic for counting instructions", "instructions", 1 },
	{ "no_ops",               "Stat no_ops", "instructions", 1},
	{ "effective_mem_lat" , "Stat effective memory access latency", "cycles",1},
	// atomic instructions
	{ "atomic_instr_count",   "Statistic for counting atomic instructions", "instructions", 1 },
	{ "a_add",   							"Statistic for counting atomic add instructions", "instructions", 1 },
	{ "a_adc",   							"Statistic for counting atomic add carry instructions", "instructions", 1 },
	{ "a_and",   							"Statistic for counting atomic and instructions", "instructions", 1 },
	{ "a_btc",   							"Statistic for counting atomic bit test and complement instructions", "instructions", 1 },
	{ "a_btr",   							"Statistic for counting atomic bit test and reset instructions", "instructions", 1 },
	{ "a_bts",   							"Statistic for counting atomic bit test and set instructions", "instructions", 1 },
	{ "a_xchg",   							"Statistic for counting atomic exchange (4 types) instructions", "instructions", 1 },
	{ "a_cmpxchg",   						"Statistic for counting atomic exchange (4 types) instructions", "instructions", 1 },
	{ "a_dec",   							"Statistic for counting atomic decrement instructions", "instructions", 1 },
	{ "a_inc",   							"Statistic for counting atomic increment instructions", "instructions", 1 },
	{ "a_neg",   							"Statistic for counting atomic neg instructions", "instructions", 1 },
	{ "a_not",   							"Statistic for counting atomic not instructions", "instructions", 1 },
	{ "a_or",   							"Statistic for counting atomic or instructions", "instructions", 1 },
	{ "a_sbb",   							"Statistic for counting atomic sbb instructions", "instructions", 1 },
	{ "a_sub",   							"Statistic for counting atomic sub instructions", "instructions", 1 },
	{ "a_xor",   							"Statistic for counting atomic xor instructions", "instructions", 1 },
	{ "a_xadd",   							"Statistic for counting atomic xadd instructions", "instructions", 1 },
	{ "a_atomify",							"Statistic for counting atomic atomify instructions", "instructions", 1},
	{ NULL, NULL, NULL, 0 }
};

static const ElementInfoParam prospero_params[] = {
	{ "verbose", "Verbosity for debugging. Increased numbers for increased verbosity.", "0" },
	{ "cache_line_size", "Sets the length of the cache line in bytes, this should match the L1 cache", "64" },
	{ "reader",  "The trace reader module to load", "prospero.ProsperoTextTraceReader" },
	{ "pagesize", "Sets the page size for the Prospero simple virtual memory manager", "4096"},
	{ "clock", "Sets the clock of the core", "2GHz"} ,
	{ "max_outstanding", "Sets the maximum number of outstanding transactions that the memory system will allow", "16"},
	{ "max_issue_per_cycle", "Sets the maximum number of new transactions that the system can issue per cycle", "2"},
	{ "profileatomics", "Profile atomic instructions, 0 = none, >0 = enable", "0"},
	{ "waitCycle", "Issue requests based on the cycles of the trace, 1 = enable, 0 disable", "1"},
	{ "pimsupport", "Toggle Processing-in-Memory support on the cores, 0 = none, >0 = enable", "0"},
	{ NULL, NULL, NULL }
};

static const ElementInfoParam prosperoTextReader_params[] = {
	{ "file", "Sets the file for the trace reader to use", "" },
	{ "pimsupport", "Toggle Processing-in-Memory support on the cores, 0 = none, >0 = enable", "0"},
	{ NULL, NULL, NULL }
};

static const ElementInfoParam prosperoBinaryReader_params[] = {
	{ "file", "Sets the file for the trace reader to use", "" },
	{ "pimsupport", "Toggle Processing-in-Memory support on the cores, 0 = none, >0 = enable", "0"},
	{ NULL, NULL, NULL }
};

#ifdef HAVE_LIBZ
static const ElementInfoParam prosperoCompressedBinaryReader_params[] = {
	{ "file", "Sets the file for the trace reader to use", "" },
	{ "pimsupport", "Toggle Processing-in-Memory support on the cores, 0 = none, >0 = enable", "0"},
	{ NULL, NULL, NULL }
};
#endif

static const ElementInfoPort prospero_ports[] = {
	{ "cache_link", "Link to the memHierarchy cache", NULL },
	{ "cramsim_cache_link", "Link to the cramsim cache", NULL },
	{"linkMemContent", "cores' link to send memory content to a memory model", NULL},
	{"pageLink", "cores' link to send the page allocation request", NULL},
	{ NULL, NULL, NULL }
};

static const ElementInfoSubComponent subcomponents[] = {
	{
		"ProsperoTextTraceReader",
		"Reads a trace from a text file",
		NULL,
		create_TextTraceReader,
		prosperoTextReader_params,
		NULL,
		"SST::Prospero::ProsperoTraceReader"
	},
	{
		"ProsperoBinaryTraceReader",
		"Reads a trace from a binary file",
		NULL,
		create_BinaryTraceReader,
		prosperoBinaryReader_params,
		NULL,
		"SST::Prospero::ProsperoTraceReader"
	},
#ifdef HAVE_LIBZ
	{
		"ProsperoCompressedBinaryTraceReader",
		"Reads a trace from a compressed binary file",
		NULL,
		create_CompressedBinaryTraceReader,
		prosperoCompressedBinaryReader_params,
		NULL,
		"SST::Prospero::ProsperoTraceReader"
	},
#endif
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

static const ElementInfoComponent components[] = {
	{
		"prosperoCPU",
		"Trace-based CPU model",
		NULL,
		create_ProsperoCPU,
		prospero_params,
		prospero_ports,
		COMPONENT_CATEGORY_PROCESSOR,
		prospero_statistics
	},
	{ NULL, NULL, NULL, NULL, NULL, NULL, 0 }
};

extern "C" {
	ElementLibraryInfo prospero_eli = {
		"prospero",
		"Trace-based CPU models",
		components,
		NULL, // Events
		NULL, // Introspectors
		NULL, // Modules
		subcomponents,
		NULL,
		NULL
	};
}
