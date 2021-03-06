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


#ifndef _H_SS_PROSPERO_MEM_MGR
#define _H_SS_PROSPERO_MEM_MGR

#include <sst/core/output.h>
#include <map>
#include <queue>

namespace SST {
namespace Prospero {

class ProsperoMemoryManager {
public:
	ProsperoMemoryManager(const uint64_t pageSize, const uint64_t pageCnt, Output* output, int cpuid);

	~ProsperoMemoryManager();
	uint64_t translate(const uint64_t virtAddr);
	void pushNextPageNum(uint64_t nextPageNum);
	bool isPageAllocated(const uint64_t virtAddr);
	void fillPageTable(uint64_t virtPageStart, uint64_t phyPageStart);

private:

	std::map<uint64_t, uint64_t> pageTable;
	std::queue<uint64_t>nextPageList;
	uint64_t m_nextPageStart;
	uint64_t pageSize;
	uint64_t pageCnt;
	Output* output;
	int m_cpuid;
};

}
}

#endif

