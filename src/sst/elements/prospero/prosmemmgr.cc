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
#include "prosmemmgr.h"
#include "assert.h"
#include "time.h"

using namespace SST::Prospero;

ProsperoMemoryManager::ProsperoMemoryManager(const uint64_t pgSize, const uint64_t pgCnt, Output* out, int cpuid) :
	pageSize(pgSize),pageCnt(pgCnt) {

	output = out;
	m_nextPageStart = 0;
	m_cpuid=cpuid;
	srand(cpuid);
}

ProsperoMemoryManager::~ProsperoMemoryManager() {

}


void ProsperoMemoryManager::fillPageTable(uint64_t virtAddr, uint64_t phyPageStart)
{
	const uint64_t pageOffset = virtAddr % pageSize;
	const uint64_t virtPageStart = virtAddr - pageOffset;

	//assert(nextPageList.size()>0);

	output->verbose(CALL_INFO, 4, 0, "[CORE ID:%d] insert a page to table table for virtual address %" PRIu64 ", page offset=%" PRIu64 ", start virt=%" PRIu64 "\n",
					m_cpuid,virtAddr, pageOffset, virtPageStart);

	pageTable.insert( std::pair<uint64_t, uint64_t>(virtPageStart, phyPageStart));
}

bool ProsperoMemoryManager::isPageAllocated(const uint64_t virtAddr)
{
	const uint64_t pageOffset = virtAddr % pageSize;
	const uint64_t virtPageStart = virtAddr - pageOffset;

	//assert(nextPageList.size()>0);

	output->verbose(CALL_INFO, 4, 0, "check if a page is allocated for virtual address %" PRIu64 ", page offset=%" PRIu64 ", start virt=%" PRIu64 "\n",
					virtAddr, pageOffset, virtPageStart);

	std::map<uint64_t, uint64_t>::iterator findEntry = pageTable.find(virtPageStart);
	return findEntry!=pageTable.end();
}


uint64_t ProsperoMemoryManager::translate(const uint64_t virtAddr) {
	const uint64_t pageOffset = virtAddr % pageSize;
	const uint64_t virtPageStart = virtAddr - pageOffset;
	uint64_t resolvedPhysPageStart = 0;

	output->verbose(CALL_INFO, 4, 0, "[CORE ID:%d] Translating virtual address %" PRIu64 ", page offset=%" PRIu64 ", start virt=%" PRIu64 "\n",
					m_cpuid, virtAddr, pageOffset, virtPageStart);

	std::map<uint64_t, uint64_t>::iterator findEntry = pageTable.find(virtPageStart);
	if(findEntry == pageTable.end()) {
		uint64_t nextPageStart=0;

		output->verbose(CALL_INFO, 4, 0, "[CORE ID:%d] Translation requires new page, creating at physical: %" PRIu64 "\n", nextPageStart);

		resolvedPhysPageStart = nextPageStart;
		pageTable.insert( std::pair<uint64_t, uint64_t>(virtPageStart, nextPageStart) );
		m_nextPageStart+=pageSize;

	} else {
		resolvedPhysPageStart = findEntry->second;
	}

	output->verbose(CALL_INFO, 4, 0, "[CORE ID:%d] Translated physical page to %" PRIu64 " + offset %" PRIu64 " = final physical %" PRIu64 "\n",
		m_cpuid, resolvedPhysPageStart, pageOffset, (resolvedPhysPageStart + pageOffset));

	// Reapply the offset to the physical page we just located and we are finished
	return (resolvedPhysPageStart + pageOffset);
}
