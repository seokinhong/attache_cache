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
#include <sst/core/unitAlgebra.h>

#include <sst/elements/memHierarchy/memEvent.h>
#include "proscpu.h"

#include <algorithm>

using namespace SST;
using namespace SST::Prospero;

#define PROSPERO_MAX(a, b) ((a) < (b) ? (b) : (a))

ProsperoComponent::ProsperoComponent(ComponentId_t id, Params& params) :
	Component(id) {

		const uint32_t output_level = (uint32_t) params.find<uint32_t>("verbose", 0);
		output = new SST::Output("Prospero[@p:@l]: ", output_level, 0, SST::Output::STDOUT);

		std::string traceModule = params.find<std::string>("reader", "prospero.ProsperoTextTraceReader");
		output->verbose(CALL_INFO, 1, 0, "Reader module is: %s\n", traceModule.c_str());

		Params readerParams = params.find_prefix_params("readerParams.");
		reader = dynamic_cast<ProsperoTraceReader*>( loadSubComponent(traceModule, this, readerParams) );

		if(NULL == reader) {
			output->fatal(CALL_INFO, -1, "Failed to load reader module: %s\n", traceModule.c_str());
		}

		reader->setOutput(output);

		pageSize = (uint64_t) params.find<uint64_t>("pagesize", 4096);
		output->verbose(CALL_INFO, 1, 0, "Configured Prospero page size for %" PRIu64 " bytes.\n", pageSize);

		int pageCnt = (uint64_t) params.find<uint64_t>("pageCount", 1000000);
		output->verbose(CALL_INFO, 1, 0, "Configured Prospero page count\n", pageCnt);

		cacheLineSize = (uint64_t) params.find<uint64_t>("cache_line_size", 64);
		output->verbose(CALL_INFO, 1, 0, "Configured Prospero cache line size for %" PRIu64 " bytes.\n", cacheLineSize);

		std::string prosClock = params.find<std::string>("clock", "2GHz");
		// Register the clock
		registerClock(prosClock, new Clock::Handler<ProsperoComponent>(this, &ProsperoComponent::tick));

		output->verbose(CALL_INFO, 1, 0, "Configured Prospero clock for %s\n", prosClock.c_str());

		maxOutstanding = (uint32_t) params.find<uint32_t>("max_outstanding", 16);
		output->verbose(CALL_INFO, 1, 0, "Configured maximum outstanding transactions for %" PRIu32 "\n", (uint32_t) maxOutstanding);

		maxIssuePerCycle = (uint32_t) params.find<uint32_t>("max_issue_per_cycle", 2);
		output->verbose(CALL_INFO, 1, 0, "Configured maximum transaction issue per cycle %" PRIu32 "\n", (uint32_t) maxIssuePerCycle);

		skip_cycle = (uint64_t) params.find<uint64_t>("skip_cycle", 0);

		hasROB = (bool) params.find<bool>("hasROB",false);
		sizeROB = (uint32_t) params.find<uint32_t>("sizeROB", 32);
		maxCommitPerCycle =(uint32_t) params.find<uint32_t>("maxCommitPerCycle",4);

		if(hasROB)
		{
			ROB.clear();
		}
		bool l_found=false;
		cpuid = (uint32_t) params.find<uint32_t>("cpuid", 0, l_found);
		if(l_found==false)
		{
			fprintf(stderr,"[Prospero] cpuid is not specified\n");
			exit(-1);
		}

		if(cpuid==0)
		{
			// tell the simulator not to end without us
			registerAsPrimaryComponent();
			primaryComponentDoNotEndSim();
		}


		output->verbose(CALL_INFO, 1, 0, "Configuring Prospero cache connection...\n");
		cache_link = dynamic_cast<SimpleMem*>(loadModuleWithComponent("memHierarchy.memInterface", this, params));
		cache_link->initialize("cache_link", new SimpleMem::Handler<ProsperoComponent>(this, &ProsperoComponent::handleResponse) );

		output->verbose(CALL_INFO, 1, 0, "Configuration of memory interface completed.\n");

		output->verbose(CALL_INFO, 1, 0, "Reading first entry from the trace reader...\n");
		currentEntry = reader->readNextEntry();
		output->verbose(CALL_INFO, 1, 0, "Read of first entry complete.\n");

		output->verbose(CALL_INFO, 1, 0, "Creating memory manager with page size %" PRIu64 "...\n", pageSize);
		memMgr = new ProsperoMemoryManager(pageSize, pageCnt, output,cpuid);
		output->verbose(CALL_INFO, 1, 0, "Created memory manager successfully.\n");

		// atomic instructions support
		profileAtomics = (uint32_t) params.find<uint32_t>("profileatomics", 0);

		switch(profileAtomics) {
			case 1:
				output->verbose(CALL_INFO, 1, 0, "Profiling of atomic instructions is Enabled.\n");
				break;
			default:
				output->verbose(CALL_INFO, 1, 0, "Profiling of atomic instructions Disabled.\n");
				break;
		}

		pimSupport = (uint32_t) params.find<uint32_t>("pimsupport", 0);

		if ( profileAtomics == 0 && pimSupport > 0 ){
			output->fatal(CALL_INFO, -1, "PIM support requires atomic instruction profiling to be enabled.\n");
		}

		switch(pimSupport) {
			case 1:
			case 2:
				output->verbose(CALL_INFO, 1, 0, "PIM support is Enabled.\n");
				break;
			default:
				output->verbose(CALL_INFO, 1, 0, "PIM support is Disabled.\n");
				break;
		}

		waitForCycle = (uint32_t) params.find<uint32_t>("waitCycle", 1);
		switch(waitForCycle) {
			case 1:
				output->verbose(CALL_INFO, 1, 0, "Follow the trace cycles is Enabled.\n");
				break;
			default:
				output->verbose(CALL_INFO, 1, 0, "Follow the trace cycles is Disabled.\n");
				break;
		}

		// We start by telling the system to continue to process as long as the first entry
		// is not NULL
		traceEnded = currentEntry == NULL;
		baseCycle=0;

		readsIssued = 0;
		writesIssued = 0;
		splitReadsIssued = 0;
		splitWritesIssued = 0;
		totalBytesRead = 0;
		totalBytesWritten = 0;

		currentOutstanding = 0;
		cyclesWithNoIssue = 0;
		cyclesWithIssue = 0;
		cyclesNoInstr = 0;
		cyclesDrift = 0;
		issuedAtomic = 0;
		currentOutstandingUC = 0;
		NonMemInstCnt=0;
		cyclesLsqFull=0;
		cyclesRobFull=0;

		subID = (char*) malloc(sizeof(char) * 32);
		sprintf(subID, "%" PRIu64, id);

		statReadRequests  = this->registerStatistic<uint64_t>( "read_requests", subID );
		statWriteRequests = this->registerStatistic<uint64_t>( "write_requests", subID );
		statSplitReadRequests = this->registerStatistic<uint64_t>( "split_read_requests", subID );
		statSplitWriteRequests = this->registerStatistic<uint64_t>( "split_write_requests", subID );
		statCyclesIssue = this->registerStatistic<uint64_t>( "cycles_issue", subID );
		statCyclesNoIssue = this->registerStatistic<uint64_t>( "cycles_no_issue", subID );
		statCyclesTlbMiss = this->registerStatistic<uint64_t>( "cycles_tlb_misses", subID );
		statCyclesNoInstr = this->registerStatistic<uint64_t>( "cycles_no_instr", subID );
		statCycles = this->registerStatistic<uint64_t>( "cycles", subID );
		statBytesRead  = this->registerStatistic<uint64_t>( "bytes_read", subID );
		statBytesWritten  = this->registerStatistic<uint64_t>( "bytes_written", subID );
		statInstructionCount = this->registerStatistic<uint64_t>( "instruction_count", subID );
		statCyclesLsqFull= this->registerStatistic<uint64_t>( "cycles_lsq_full", subID );
		statCyclesRobFull = this->registerStatistic<uint64_t>( "cycles_rob_full", subID );

		// atomic instructions
		statAtomicInstrCount = this->registerStatistic<uint64_t>( "atomic_instr_count", subID );
		statA_AddIns = this->registerStatistic<uint64_t>("a_add", subID);
		statA_AdcIns = this->registerStatistic<uint64_t>("a_adc", subID);
		statA_AndIns = this->registerStatistic<uint64_t>("a_and", subID);
		statA_BtcIns = this->registerStatistic<uint64_t>("a_btc", subID);
		statA_BtrIns = this->registerStatistic<uint64_t>("a_btr", subID);
		statA_BtsIns = this->registerStatistic<uint64_t>("a_bts", subID);
		statA_XchgIns = this->registerStatistic<uint64_t>("a_xchg", subID);
		statA_CMPXchgIns = this->registerStatistic<uint64_t>("a_cmpxchg", subID);
		statA_DecIns = this->registerStatistic<uint64_t>("a_dec", subID);
		statA_IncIns = this->registerStatistic<uint64_t>("a_inc", subID);
		statA_NegIns = this->registerStatistic<uint64_t>("a_neg", subID);
		statA_NotIns = this->registerStatistic<uint64_t>("a_not", subID);
		statA_OrIns = this->registerStatistic<uint64_t>("a_or", subID);
		statA_SbbIns = this->registerStatistic<uint64_t>("a_sbb", subID);
		statA_SubIns = this->registerStatistic<uint64_t>("a_sub", subID);
		statA_XorIns = this->registerStatistic<uint64_t>("a_xor", subID);
		statA_XaddIns = this->registerStatistic<uint64_t>("a_xadd", subID);
		statA_Atomify = this->registerStatistic<uint64_t>("a_atomify", subID);

		output->verbose(CALL_INFO, 1, 0, "Prospero configuration completed successfully.\n");


	bool pagelink_en = (bool)params.find<bool>("pagelink_en",false);
	if (pagelink_en)
		page_link = configureLink("pageLink", new Event::Handler<ProsperoComponent>(this, &ProsperoComponent::handlePageAllocResponse) );
	else
		page_link=0;

    bool content_link_en = (bool)params.find<bool>("memcontent_link_en",false);
    if (content_link_en){
        cpu_to_mem_link = configureLink("linkMemContent");

    } else
        cpu_to_mem_link = 0;

	max_inst= (uint64_t)params.find<uint64_t>("max_inst",0);
	committed_inst=0;
	isPageRequestSent=false;
	m_inst=0;
    sim_started=false;
}


void ProsperoComponent::sendMemContent(uint64_t addr, uint64_t vaddr, uint32_t size, uint64_t data) {

	std::vector<uint8_t> data_tmp;
	for(int i=0;i<8;i++) {
		uint8_t tmp=(uint8_t)(data>>8*i);
		data_tmp.push_back(tmp);
	}
    SST::MemHierarchy::MemEvent *req = new SST::MemHierarchy::MemEvent(this,addr,vaddr,SST::MemHierarchy::Command::Put,data_tmp);
	//printf("sender [%s], addr:%llx cline addr: %llx size:%d\n",this->getName().c_str(),addr,(addr>>6)<<6,data);
	req->setVirtualAddress(vaddr);
    req->setDst("MemController0");
	cpu_to_mem_link->send(req);
}


void ProsperoComponent::sendPageAllocRequest(uint64_t addr) {
	if(isPageRequestSent==false) {
		SST::MemHierarchy::MemEvent *req = new SST::MemHierarchy::MemEvent(this, addr, addr,
																		   SST::MemHierarchy::Command::Get);
		page_link->send(req);
		isPageRequestSent = true;
	}
}


void ProsperoComponent::handlePageAllocResponse(Event *ev) {

	SST::MemHierarchy::MemEvent* res=dynamic_cast<SST::MemHierarchy::MemEvent*>(ev);
	SST::MemHierarchy::Command cmd = res->getCmd();
	assert(cmd==SST::MemHierarchy::Command::GetSResp);
	uint64_t nextPageNum=0;
	std::vector<uint8_t> recv_data=(std::vector<uint8_t>)res->getPayload();

	for(int i=0;i<8;i++)
	{
		uint64_t tmp=(uint64_t)recv_data[i];
		nextPageNum+=(tmp<<8*i);
	}
	//printf("test next page start address: %llx\n",nextPageNum);
	memMgr->fillPageTable(res->getAddr(), nextPageNum);

	delete ev;
	isPageRequestSent=false;
}


ProsperoComponent::~ProsperoComponent() {
	delete memMgr;
	delete output;
}

void ProsperoComponent::init(unsigned int phase) {
	    cache_link->init(phase);
	}


void ProsperoComponent::finish() {
	const uint64_t nanoSeconds = getCurrentSimTimeNano();

	output->output("\n");
	output->output("Prospero Component Statistics:\n");

	output->output("------------------------------------------------------------------------\n");
	output->output("- Completed at:                          %" PRIu64 " ns\n", nanoSeconds);
	output->output("- Cycles with ops issued:                %" PRIu64 " cycles\n", cyclesWithIssue);
	output->output("- Cycles with no ops issued (LS full):   %" PRIu64 " cycles\n", cyclesWithNoIssue);
	output->output("- Cycles with no ops issued (No Instr):  %" PRIu64 " cycles\n", cyclesNoInstr);
	output->output("- Cycles with no ops issued (TLB Miss):  %" PRIu64 " cycles\n", cyclesTLBmiss);
	output->output("- Total Cycles:  			 %" PRIu64 " cycles\n", cyclesNoInstr+cyclesWithNoIssue+cyclesWithIssue);


	output->output("------------------------------------------------------------------------\n");
	output->output("- Reads issued:                          %" PRIu64 "\n", readsIssued);
	output->output("- Writes issued:                         %" PRIu64 "\n", writesIssued);
	output->output("- Split reads issued:                    %" PRIu64 "\n", splitReadsIssued);
	output->output("- Split writes issued:                   %" PRIu64 "\n", splitWritesIssued);
	output->output("- Bypassed atomic ops:   	 	 %" PRIu64 "\n", issuedAtomic);
	output->output("- Bytes read:                            %" PRIu64 "\n", totalBytesRead);
	output->output("- Bytes written:                         %" PRIu64 "\n", totalBytesWritten);

	output->output("------------------------------------------------------------------------\n");

	const double totalBytesReadDbl = (double) totalBytesRead;
	const double totalBytesWrittenDbl = (double) totalBytesWritten;
	const double secondsDbl = ((double) nanoSeconds) / 1000000000.0;

	char buffBWRead[32];
	sprintf(buffBWRead, "%f B/s", ((double) totalBytesReadDbl / secondsDbl));

	UnitAlgebra baBWRead(buffBWRead);

	output->output("- Bandwidth (read):                      %s\n",
			baBWRead.toStringBestSI().c_str());

	char buffBWWrite[32];
	sprintf(buffBWWrite, "%f B/s", ((double) totalBytesWrittenDbl / secondsDbl));

	UnitAlgebra uaBWWrite(buffBWWrite);

	output->output("- Bandwidth (written):                   %s\n",
			uaBWWrite.toStringBestSI().c_str());

	char buffBWCombined[32];
	sprintf(buffBWCombined, "%f B/s", ((double) (totalBytesReadDbl + totalBytesWrittenDbl) / secondsDbl));

	UnitAlgebra uaBWCombined(buffBWCombined);

	output->output("- Bandwidth (combined):                  %s\n", uaBWCombined.toStringBestSI().c_str());

	output->output("- Avr. Read request size:                %20.2f bytes\n", ((double) PROSPERO_MAX(totalBytesRead, 1)) / ((double) PROSPERO_MAX(readsIssued, 1)));
	output->output("- Avr. Write request size:               %20.2f bytes\n", ((double) PROSPERO_MAX(totalBytesWritten, 1)) / ((double) PROSPERO_MAX(writesIssued, 1)));
	output->output("- Avr. Request size:                     %20.2f bytes\n", ((double) PROSPERO_MAX(totalBytesRead + totalBytesWritten, 1)) / ((double) PROSPERO_MAX(readsIssued + writesIssued, 1)));
	output->output("\n");
}

void ProsperoComponent::handleResponse(SimpleMem::Request *ev) {
	output->verbose(CALL_INFO, 4, 0, "Handle response from memory subsystem.\n");
	if(ev->flags & SimpleMem::Request::F_NONCACHEABLE){
		output->verbose(CALL_INFO, 4, 0, "Received response from uncacheable id %" PRIu64".\n", ev->id);
		currentOutstandingUC--;
	}
	else{
		output->verbose(CALL_INFO, 4, 0, "Received reponse from cacheable id %" PRIu64 ".\n", ev->id);
		currentOutstanding--;
	}

	//set done flag in the corresponding entry of rob
	if(hasROB) {
		for (auto &it: ROB) {
			if (it.isMemory && it.id == ev->addrs[0])
#ifdef DEBUG_ROB
				printf("mem inst is completed in rob, addr:%llx\n",it.id);
#endif
				it.done = true;
		}
	}

	// Our responsibility to delete incoming event
	delete ev;
}



bool ProsperoComponent::tick(SST::Cycle_t currentCycle) {
	if(NULL == currentEntry) {
		output->verbose(CALL_INFO, 16, 0, "Prospero execute on cycle %" PRIu64 ", current entry is NULL, outstanding=%" PRIu32 ", outstandingUC=%" PRIu32 ", maxOut=%" PRIu32 "\n", (uint64_t) currentCycle, currentOutstanding, currentOutstandingUC, maxOutstanding);
	} else {
		output->verbose(CALL_INFO, 16, 0, "Prospero execute on cycle %" PRIu64 ", current entry time: %" PRIu64 ", outstanding=%" PRIu32 ", outstandingUC=%" PRIu32 ", maxOut=%" PRIu32 "\n", (uint64_t) currentCycle, (uint64_t) currentEntry->getIssueAtCycle(), currentOutstanding, currentOutstandingUC, maxOutstanding);
	}

	// If we have finished reading the trace we need to let the events in flight
	// drain and the system come to a rest
	if(traceEnded) {
		if(cpuid==0) {
			statCycles->addData(1);
			cyclesNoInstr++;
			statCyclesNoInstr->addData(1);

			if (0 == currentOutstanding && currentOutstandingUC == 0) {
				primaryComponentOKToEndSim();
				return true;
			}
			return false;
		}
		else  //run again until the first core finish reading the trace
		{
			reader->resetTrace();
			currentEntry = reader->readNextEntry();
			traceEnded=false;
			baseCycle=currentCycle;
		}
	}
	if(sim_started==false)
	{
		skip_cycle=currentEntry->getIssueAtCycle();
		sim_started=true;
                printf("[Prospero] Simulation is started for core %d\n",cpuid);
                fflush(0);
	}

	uint64_t currentEntryCycle=currentEntry->getIssueAtCycle()+baseCycle;
	uint64_t l_currentCycle=currentCycle+skip_cycle;


	const uint64_t outstandingBeforeIssue = currentOutstanding;
	const uint64_t outstandingBeforeIssueUC = currentOutstandingUC;
	const uint64_t issuedAtomicBefore = issuedAtomic;
	bool ls_full = false;
	bool ls_tlb_miss=false;
	bool rob_full = false;

	//commit instructions;
    if(hasROB)
    {

		for(uint32_t i=0;i< maxCommitPerCycle&&!ROB.empty(); i++)
		{
            if (!ROB.front().isMemory) {
                ROB.pop_front();
#ifdef DEBUG_ROB
                printf("[%lld]non inst committed\n", currentCycle);
#endif
            } else {
                if (ROB.front().done) {
#ifdef DEBUG_ROB
                    printf("[%lld]mem inst committed, address:%llx\n", currentCycle, ROB.front().id);
#endif
                    ROB.pop_front();
                }
            }
		}
#ifdef DEBUG_ROB
		printf("------------------------------------------------\n");
		printf("[ROB status] cycle:%lld, num rob:%d\n",currentCycle,ROB.size());
		printf("mem\tid\tdone\n");
		for(auto &it: ROB)
		{
			printf("%s\t%llx\t%s\n",it.isMemory?"true":"false",it.id,it.done?"true":"false");
		}
#endif
    }
	// Wait to see if the current operation can be issued, if yes then
	// go ahead and issue it, otherwise we will stall
	for(uint32_t i = 0; i < maxIssuePerCycle; ++i) {
			if (m_inst % 10000 == 0) {
						printf("# of issued inst: %lld\n", m_inst);
						fflush(0);
			}
		if(currentOutstanding < maxOutstanding) {

			if (waitForCycle == 0) {
					m_inst++;

                    //insert non memory instruction to rob
                    if(hasROB)
					{
						if(ROB.size()< sizeROB) {
							if (NonMemInstCnt > 0) {
#ifdef DEBUG_ROB
								printf("[%lld]non mem inst issued\n",currentCycle);
#endif
								ROB_ENTRY entry;
								entry.isMemory = false;
								entry.done=true;
								entry.id=0;
								ROB.push_back(entry);
								NonMemInstCnt--;
								continue;
							} else {

								if (page_link) {
									if (isPageRequestSent == true) {
										ls_tlb_miss=true;
										break;
									}
									else if (memMgr->isPageAllocated(currentEntry->getAddress()) == false) {
										sendPageAllocRequest(currentEntry->getAddress());
										ls_tlb_miss=true;
										break;
									}
								}

								uint64_t old_instnum = currentEntry->getInstNum();
								ROB_ENTRY entry;
								entry.id = currentEntry->getAddress();
								entry.isMemory = true;
								entry.done = false;
								ROB.push_back(entry);

								// Issue the pending request into the memory subsystem
								issueRequest(currentEntry);

								// Obtain the next newest request
								currentEntry = reader->readNextEntry();

								//update the number of non mem inst cnt
								NonMemInstCnt = currentEntry->getInstNum() - old_instnum;
#ifdef DEBUG_ROB
								printf("[%lld]mem inst issued, address:%lx, dependent inst:%lld\n",currentCycle,entry.id,NonMemInstCnt);
#endif

								// Trace reader has read all entries, time to begin draining
								// the system, caches etc
								if (NULL == currentEntry) {
									traceEnded = true;
									break;
								}
							}
						}
						else //ROB is full
						{
							rob_full=true;
							break;
						}
                    }
					else
					{
						if (page_link) {
							if (isPageRequestSent == true) {
								ls_tlb_miss=true;
								break;
							}
							else if (memMgr->isPageAllocated(currentEntry->getAddress()) == false) {
								sendPageAllocRequest(currentEntry->getAddress());
								ls_tlb_miss=true;
								break;
							}
						}

						// Issue the pending request into the memory subsystem
						issueRequest(currentEntry);

						// Obtain the next newest request
						currentEntry = reader->readNextEntry();

							// Trace reader has read all entries, time to begin draining
						// the system, caches etc
						if (NULL == currentEntry) {
							traceEnded = true;
							break;
						}
					}
			} else {
				if (page_link) {
					if (isPageRequestSent == true) {
						ls_tlb_miss=true;
						break;
					}
					else if (memMgr->isPageAllocated(currentEntry->getAddress()) == false) {
						sendPageAllocRequest(currentEntry->getAddress());
						ls_tlb_miss=true;
						break;
					}
				}

				//std::cout<<l_currentCycle<<" "<<currentEntryCycle<<std::endl;
				if (l_currentCycle >= currentEntryCycle) {
						m_inst++;
						if (m_inst % 1000000 == 0) {
							printf("[core%d] # of issued memory inst: %lld\n", cpuid, m_inst);
							fflush(0);
						}

						uint64_t instNum=currentEntry->getInstNum();
						if (instNum % 1000000 >0 && instNum%1000000 < 10) {
							printf("[core%d] # of issued inst: %lld\n", cpuid,instNum);
							fflush(0);
						}
						// Issue the pending request into the memory subsystem
						issueRequest(currentEntry);

						// Obtain the next newest request
						currentEntry = reader->readNextEntry();

						// Trace reader has read all entries, time to begin draining
						// the system, caches etc
						if (NULL == currentEntry) {
							traceEnded = true;
							break;
						}

				} else {
					output->verbose(CALL_INFO, 8, 0,
									"Not issuing on cycle %" PRIu64 ", waiting for cycle: %" PRIu64 "\n",
									(uint64_t) currentCycle, currentEntry->getIssueAtCycle());
					// Have reached a point in the trace which is too far ahead in time
					// so stall until we find that point
					break;
				}
			}
		} else{
			// Cannot issue any more items this cycle, load/stores are full
			ls_full = true;
			break;
		}
	}

	const uint64_t outstandingAfterIssue = currentOutstanding;
	const uint64_t outstandingAfterIssueUC = currentOutstandingUC;
	const uint64_t issuedThisCycle = outstandingAfterIssue - outstandingBeforeIssue + outstandingAfterIssueUC - outstandingBeforeIssueUC  + issuedAtomic - issuedAtomicBefore;

	if(0 == issuedThisCycle) {
		if (ls_full || rob_full){
			if(ls_full) {
				cyclesLsqFull++;
				statCyclesLsqFull->addData(1);
			}
			else {
				cyclesRobFull++;
				statCyclesRobFull->addData(1);
			}

			cyclesWithNoIssue++;
			statCyclesNoIssue->addData(1);
		}
		else if(ls_tlb_miss)
		{
			cyclesTLBmiss++;
			statCyclesTlbMiss->addData(1);
		}
		else{
			cyclesNoInstr++;
			statCyclesNoInstr->addData(1);
		}
	} else {

		cyclesWithIssue++;
		statCyclesIssue->addData(1);
	}
	statCycles->addData(1);

	// Keep simulation ticking, we have more work to do if we reach here
	return false;
}

void ProsperoComponent::issueRequest(const ProsperoTraceEntry* entry) {
	const uint64_t entryInstNum = entry->getInstNum();
	const uint64_t entryAddress = entry->getAddress();
	const uint64_t entryLength  = (uint64_t) entry->getLength();

	const uint64_t lineOffset   = entryAddress % cacheLineSize;
	bool  isRead                = entry->isRead();
	bool isAtomic		= entry->isAtomic();
	const uint32_t atomicClass	= entry->getAtomic();
    std::vector<uint64_t> data_vector = entry->getDataVector();

/*	std::cout<<entry->getCycle()<<" ";
    std::cout<<std::hex<<entryInstNum;
	std::cout<<" "<<std::hex<<entryAddress;
	std::cout<<" "<<isRead;

	for(auto& it:data_vector)
        std::cout<< " "<< std::hex<<it;
    std::cout<<std::endl;
*/

	if(max_inst>0 && m_inst>max_inst)
	{
		traceEnded=true;
	}
/*	if(max_inst>0 && entryInstNum>max_inst)
	{
		traceEnded=true;
	}
*/
	if(lineOffset + entryLength > cacheLineSize) {
		// Perform a split cache line load
		const uint64_t lowerLength = cacheLineSize - lineOffset;
		const uint64_t upperLength = entryLength - lowerLength;

		if(lowerLength + upperLength != entryLength) {
			output->fatal(CALL_INFO, -1, "Error: split cache line, lower size=%" PRIu64 ", upper size=%" PRIu64 " != request length: %" PRIu64 " (cache line %" PRIu64 ")\n", lowerLength, upperLength, entryLength, cacheLineSize);
		}
		assert(lowerLength + upperLength == entryLength);

		// Start split requests at the original requested address and then
		// also the the next cache line along
		const uint64_t lowerAddress = memMgr->translate(entryAddress);
		const uint64_t entryAddress_upper=((lowerAddress - (lowerAddress % cacheLineSize)) + cacheLineSize);
		const uint64_t upperAddress = memMgr->translate(entryAddress_upper);

		SimpleMem::Request* reqLower = new SimpleMem::Request( isRead ? SimpleMem::Request::Read : SimpleMem::Request::Write, lowerAddress, lowerLength);
		reqLower->setVirtualAddress(entryAddress);

		SimpleMem::Request* reqUpper = new SimpleMem::Request(isRead ? SimpleMem::Request::Read : SimpleMem::Request::Write, upperAddress, upperLength);
		reqUpper->setVirtualAddress((entryAddress - (entryAddress % cacheLineSize)) + cacheLineSize);


		if(cpu_to_mem_link) {
			sendMemContent(lowerAddress, entryAddress, 64, entry->getData());
			sendMemContent(upperAddress, entryAddress_upper, 64, entry->getData());
		}

		if (pimSupport == 0 || !isAtomic){
			//Treat atomics as regular ops
			cache_link->sendRequest(reqLower);
			cache_link->sendRequest(reqUpper);
			currentOutstanding += 2;

			if(isRead) {
				readsIssued += 2;
				splitReadsIssued++;
				statReadRequests->addData(2);
				statSplitReadRequests->addData(1);
				totalBytesRead += entryLength;
				statBytesRead->addData(entryLength);

			} else {
				writesIssued += 2;
				splitWritesIssued++;
				statWriteRequests->addData(2);
				statSplitWriteRequests->addData(1);
				totalBytesWritten += entryLength;
				statBytesWritten->addData(entryLength);
			}

			if (profileAtomics && isAtomic){
				statAtomicInstrCount->addData(2);
				countAtomicInstr(atomicClass);
				countAtomicInstr(atomicClass);
			}
		}

		else if (pimSupport == 1 && isAtomic){
			//Send read and write atomics as noncacheable to the memory subsystem
			reqLower->flags |= Interfaces::SimpleMem::Request::F_NONCACHEABLE;
			reqUpper->flags |= Interfaces::SimpleMem::Request::F_NONCACHEABLE;

			cache_link->sendRequest(reqLower);
			cache_link->sendRequest(reqUpper);

			if(isRead) {
				readsIssued += 2;
				splitReadsIssued++;
				currentOutstandingUC += 2;
				statReadRequests->addData(2);
				statSplitReadRequests->addData(1);
				totalBytesRead += entryLength;
				statBytesRead->addData(entryLength);

			} else {
				writesIssued += 2;
				splitWritesIssued++;
				currentOutstandingUC += 2;
				statWriteRequests->addData(2);
				statSplitWriteRequests->addData(1);
				totalBytesWritten += entryLength;
				statBytesWritten->addData(entryLength);
			}

			if (profileAtomics){
				statAtomicInstrCount->addData(2);
				countAtomicInstr(atomicClass);
				countAtomicInstr(atomicClass);
			}
		}

		else if (pimSupport == 2 && isAtomic){
			//Send only write atomics as noncacheable to the memory subsystem
			if (!isRead){
				reqLower->flags |= Interfaces::SimpleMem::Request::F_NONCACHEABLE;
				reqUpper->flags |= Interfaces::SimpleMem::Request::F_NONCACHEABLE;
				cache_link->sendRequest(reqLower);
				cache_link->sendRequest(reqUpper);
				currentOutstandingUC += 2;

				writesIssued += 2;
				splitWritesIssued++;
				statWriteRequests->addData(2);
				statSplitWriteRequests->addData(1);
				totalBytesWritten += entryLength;
				statBytesWritten->addData(entryLength);

				if (profileAtomics){
					statAtomicInstrCount->addData(2);
					countAtomicInstr(atomicClass);
					countAtomicInstr(atomicClass);
				}
			}
			else{
				issuedAtomic += 2;
			}
		}

		else if (pimSupport == 3 && isAtomic){
			//Do not send any atomics to the memory subsystem
			issuedAtomic += 2;
		}
		else{
			//error
		}

		statInstructionCount->addData(2);
	}
	else{
		
		SimpleMem::Addr addr = memMgr->translate(entryAddress);
		SimpleMem::Request::Command cmd = isRead ? SimpleMem::Request::Read : SimpleMem::Request::Write;
		uint64_t size = entryLength;
		
		//if(isAtomic && pimSupport == 2){
		//	cmd = SimpleMem::Request::FlushLine;
		//	size = cacheLineSize;
		//	addr = addr - (addr % cacheLineSize);
		//}
		// Perform a single load
		SimpleMem::Request* request = new SimpleMem::Request(cmd, addr, size);
	       	request->setVirtualAddress(entryAddress);

		output->verbose(CALL_INFO, 8, 0, "Issuing request id: %" PRIu64 ", cacheable %" PRIu32" \n", request->id, isAtomic);

		if(cpu_to_mem_link)
			sendMemContent(addr,entryAddress,64, entry->getData());


		if (pimSupport == 0 || !isAtomic){
			//Treat atomics as regular ops
			cache_link->sendRequest(request);
			currentOutstanding += 1;

			if(isRead) {
				readsIssued += 1;
				statReadRequests->addData(1);
				totalBytesRead += entryLength;
				statBytesRead->addData(entryLength);

			} else {
				writesIssued += 1;
				statWriteRequests->addData(1);
				totalBytesWritten += entryLength;
				statBytesWritten->addData(entryLength);
			}

			if (profileAtomics && isAtomic){
				statAtomicInstrCount->addData(1);
				countAtomicInstr(atomicClass);
			}
		}

		else if (pimSupport == 1 && isAtomic){
			//Send read and write atomics as noncacheable to the memory subsystem
			request->flags |= Interfaces::SimpleMem::Request::F_NONCACHEABLE;

			cache_link->sendRequest(request);

			if(isRead) {
				readsIssued += 1;
				statReadRequests->addData(1);
				currentOutstandingUC += 1;
				totalBytesRead += entryLength;
				statBytesRead->addData(entryLength);

			} else {
				writesIssued += 1;
				statWriteRequests->addData(1);
				currentOutstandingUC += 1;
				totalBytesWritten += entryLength;
				statBytesWritten->addData(entryLength);
			}

			if (profileAtomics){
				statAtomicInstrCount->addData(1);
				countAtomicInstr(atomicClass);
			}
		}

		else if (pimSupport == 2 && isAtomic){
			//Send only write atomics as noncacheable to the memory subsystem
			if (!isRead){
				request->flags |= Interfaces::SimpleMem::Request::F_NONCACHEABLE;
				cache_link->sendRequest(request);
				currentOutstandingUC += 1;

				writesIssued += 1;
				statWriteRequests->addData(1);
				totalBytesWritten += entryLength;
				statBytesWritten->addData(entryLength);

				if (profileAtomics){
					statAtomicInstrCount->addData(1);
					countAtomicInstr(atomicClass);
				}
			}
			else{
				issuedAtomic++;
			}
		}

		else if (pimSupport == 3){
			//Do not send any atomics to the memory subsystem
			issuedAtomic++;
		}

		statInstructionCount->addData(1);
	}
	// Delete this entry, we are done converting it into a request
	delete entry;
}

void ProsperoComponent::countAtomicInstr(uint32_t opcode){
	switch(opcode){
		case ATOMIC_ADD:
			statA_AddIns->addData(1);
			break;
		case ATOMIC_ADC:
			statA_AdcIns->addData(1);
			break;
		case ATOMIC_AND:
			statA_AndIns->addData(1);
			break;
		case ATOMIC_BTC:
			statA_BtcIns->addData(1);
			break;
		case ATOMIC_BTR:
			statA_BtrIns->addData(1);
			break;
		case ATOMIC_BTS:
			statA_BtsIns->addData(1);
			break;
		case ATOMIC_XCHG:
			statA_XchgIns->addData(1);
			break;
		case ATOMIC_CMPXCHG:
			statA_CMPXchgIns->addData(1);
			break;
		case ATOMIC_DEC:
			statA_DecIns->addData(1);
			break;
		case ATOMIC_INC:
			statA_IncIns->addData(1);
			break;
		case ATOMIC_NEG:
			statA_NegIns->addData(1);
			break;
		case ATOMIC_NOT:
			statA_NotIns->addData(1);
			break;
		case ATOMIC_OR:
			statA_OrIns->addData(1);
			break;
		case ATOMIC_SBB:
			statA_SbbIns->addData(1);
			break;
		case ATOMIC_SUB:
			statA_SubIns->addData(1);
			break;
		case ATOMIC_XOR:
			statA_XorIns->addData(1);
			break;
		case ATOMIC_XADD:
			statA_XaddIns->addData(1);
			break;
		case ATOMIC_ATOMIFY:
			statA_Atomify->addData(1);
			break;
		default:
			output->fatal(CALL_INFO, -4, "Prospero %s received unrecognized atomic opcode %" PRIu32 "\n", subID, opcode);
			break;
	}
}
