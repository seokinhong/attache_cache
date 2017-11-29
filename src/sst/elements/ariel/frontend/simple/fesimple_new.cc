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

//#include <malloc.h>
#include <execinfo.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "pin.H"
#include <time.h>
#include <string.h>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <map>
#include <stack>
#include <ctime>
#include <bitset>
#include <set>
#include <sst_config.h>

#ifdef HAVE_LIBZ
//#define COMP_DEBUG

#include "zlib.h"
#define BT_PRINTF(fmt, args...) gzprintf(btfiles[thr], fmt, ##args);

#else 

#define BT_PRINTF(fmt, args...) fprintf(btfiles[thr], fmt, ##args);

#endif

//This must be defined before inclusion of intttypes.h
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include "ariel_shmem.h"
#include "ariel_inst_class.h"

#undef __STDC_FORMAT_MACROS

using namespace SST::ArielComponent;

KNOB<UINT32> TrapFunctionProfile(KNOB_MODE_WRITEONCE, "pintool",
    "t", "0", "Function profiling level (0 = disabled, 1 = enabled)");
KNOB<string> SSTNamedPipe(KNOB_MODE_WRITEONCE, "pintool",
    "p", "", "Named pipe to connect to SST simulator");
KNOB<UINT64> MaxInstructions(KNOB_MODE_WRITEONCE, "pintool",
    "i", "1000", "Maximum number of instructions to run");
KNOB<UINT64> WarmupInstructions(KNOB_MODE_WRITEONCE, "pintool",
     "w","10000", "The number of instructions to warmup");
//"w","10", "The number of instructions to warmup");
KNOB<UINT32> SSTVerbosity(KNOB_MODE_WRITEONCE, "pintool",
    "v", "0", "SST verbosity level");
KNOB<UINT32> MaxCoreCount(KNOB_MODE_WRITEONCE, "pintool",
    "c", "1", "Maximum core count to use for data pipes.");
KNOB<UINT32> StartupMode(KNOB_MODE_WRITEONCE, "pintool",
    "s", "1", "Mode for configuring profile behavior, 1 = start enabled, 0 = start disabled, 2 = attempt auto detect");
KNOB<UINT32> InterceptMultiLevelMemory(KNOB_MODE_WRITEONCE, "pintool",
    "m", "0", "Should intercept multi-level memory allocations, copies and frees, 1 = start enabled, 0 = start disabled");
KNOB<UINT32> KeepMallocStackTrace(KNOB_MODE_WRITEONCE, "pintool",
    "k", "0", "Should keep shadow stack and dump on malloc calls. 1 = enabled, 0 = disabled");
KNOB<UINT32> DefaultMemoryPool(KNOB_MODE_WRITEONCE, "pintool",
    "d", "0", "Default SST Memory Pool");
KNOB<UINT32> SimilarityDistance(KNOB_MODE_WRITEONCE, "pintool",
    "a", "0", "Default SST Memory Pool");
KNOB<UINT32> MemCompProfile(KNOB_MODE_WRITEONCE, "pintool",
    "x", "0", "Enable memory compression profiler");
KNOB<string> MemCompTraceName(KNOB_MODE_WRITEONCE, "pintool",
    "f", "comp_trace.csv", "Memory compression profile trace file name");
KNOB<string> MemCompTraceInterval(KNOB_MODE_WRITEONCE, "pintool",
    "n", "1000000", "Memory compression profile interval");

#define ARIEL_MAX(a,b) \
   ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })

typedef struct {
	int64_t insExecuted;
} ArielFunctionRecord;

UINT64 inst_cnt =0;

UINT32 funcProfileLevel;
UINT32 core_count;
UINT32 default_pool;
UINT64 warmup_insts=0;

ArielTunnel *tunnel = NULL;
bool enable_output;
bool content_copy_en=true;

std::vector<void*> allocated_list;
PIN_LOCK mainLock;
PIN_LOCK mallocIndexLock;
UINT64* lastMallocSize=NULL;
std::map<std::string, ArielFunctionRecord*> funcProfile;
UINT64* lastMallocLoc;
std::vector< std::set<ADDRINT> > instPtrsList;
UINT32 overridePool;
bool shouldOverride;

UINT64 g_similarity_comp_cnt=0;
UINT64 g_similarity_incomp_cnt=0;
UINT64 g_accessed_cacheline_num=0;
int similarity_distance=0;


typedef struct {
	UINT64 raddr;
        UINT32 rsize;
	UINT64 waddr;
        UINT32 wsize;
        ADDRINT ip;

} threadRecord;
threadRecord* thread_instr_id;

///////Memory compression//////
bool enable_memcomp;
string memcomp_tracefile;
UINT32 memcomp_interval;

#if 0
class RowCompInfo {
    private:
        UINT32 row_size;
        UINT32 cacheline_size;
        UINT32 num_cacheline;
        UINT32 compressed_size;
        std::vector<UINT32> cacheline_size;
        UINT64 num_access_cacheline25;
        UINT64 num_access_cacheline50;
        UINT64 num_access_cacheline75;
        UINT64 num_access_row25;
        UINT64 num_access_row50;
        UINT64 num_access_row75;
        UINT64 num_access;


        RowCompInfo(UINT32 _row_size, UINT32 _cacheline_size;)
        {
            row_size=_row_size;
            cacheline_size=_cacheline_size;
            num_cacheline=row_size/cacheline_size;  // cacheline is 64B

            for(UINT32 i=0; i<num_cacheline;i++)
                cacheline_size.push_back(0);
        }

        void Reset()
        {
           num_access_cacheline25=0;
           num_access_cacheline50=0;
           num_access_cacheline75=0;
           num_access_row25=0;
           num_access_row50=0;
           num_access_row75=0;
           num_access=0;
        }

        void inc_access(bool is_row, int comp_size)
        {
            int comp_ratio=0;
            if(is_row)
            {

                comp_ratio = (int)((float)row_size/(float)comp_size*100);
                fprintf(stderr,"row comp ratio: %d\n",comp_ratio);
            }
            else
            {
                comp_ratio = (int)((float)cacheline_size/(float)comp_size*100);
                fprintf(stderr,"cacheline comp ratio: %d\n",comp_ratio);
            }


        }

}

#endif

/****************************************************************/
/********************** SHADOW STACK ****************************/
/* Used by 'sieve' to associate mallocs to the code they        */
/* are called from. Turn on by turning on malloc_stack_trace    */
/****************************************************************/
/****************************************************************/

// Per-thread malloc file -> we don't have to lock the file this way
// Compress it if possible
#ifdef HAVE_LIBZ
std::vector<gzFile> btfiles;
#else
std::vector<FILE*> btfiles;
#endif

UINT64 mallocIndex;
FILE * rtnNameMap;
/* This is a record for each function call */
class StackRecord {
    private:
        ADDRINT stackPtr;
        ADDRINT target;
        ADDRINT instPtr;
    public:
        StackRecord(ADDRINT sp, ADDRINT targ, ADDRINT ip) : stackPtr(sp), target(targ), instPtr(ip) {}
        ADDRINT getStackPtr() const { return stackPtr; }
        ADDRINT getTarget() {return target;}
        ADDRINT getInstPtr() { return instPtr; }
};


uint32_t getDataSize(int64_t data_)
{
	int size=65; //bit

	if(data_==0)
		return 0;

	uint64_t data=llabs(data_);

	for(int i=1; i<64; i++)
	{
		if(data==(data & ~(0xffffffffffffffff<<i)))
		{
			size = i+1; //1 bit is used for sign bit.
			break;
		}
	}

	return size;
}

//VOID checkSimilarity(uint64_t*,uint64_t);
VOID ReadCacheLine(uint64_t addr, uint64_t * data)
{
	//assume that cache line size is 64B
#if 0
	ADDRINT addr_new = (addr);
        int copied_size= PIN_SafeCopy(data, (VOID*) addr_new, 8);
        if(copied_size !=8)
            fprintf(stderr, "ariel memory copy fail\n");
        fprintf(stderr,"addr:%lld data:%lld\n", addr, *(uint64_t*)data);

#else
	uint64_t addr_new = (addr>>6)<<6;
	int copied_size= PIN_SafeCopy((uint8_t*)data, (VOID*) addr_new, 64);
	if(copied_size !=64)
		fprintf(stderr, "ariel memory copy fail\n");


	//checkSimilarity(data,addr);
//#ifdef COMP_DEBUG
//        for(int i=0;i<8;i++)
//        {
//
//            fprintf(stderr,"[PINTOOL] cacheline read [%d] addr:%llx data:%llx \n", i, addr_new+i*8, *(data+i));
//        }
//#endif


#endif
}

uint32_t getCompressedSize(uint8_t *cacheline) {


	int min_compressed_size = 512;


	std::vector<uint64_t> data_vec;

	std::vector<uint32_t> base_size(3);
	std::map<uint32_t, uint32_t> min_delta_size_map;
	std::map<uint32_t, uint64_t> min_base_map;
	std::map<uint32_t, uint32_t> min_compressed_size_map;
	//base_size = {2, 4, 8}; //
	base_size = {4, 8}; //
	//base_size = {8}; //

	std::vector<int64_t> delta_base_vector;
	std::vector<int64_t> delta_immd_vector;
	std::vector<uint32_t> delta_flag;
	uint32_t base_id;

	uint64_t min_base = 0;
	int min_k = 0;


	for (auto &k: base_size) {
		uint8_t *ptr;
		data_vec.clear();
		for (int i = 0; i < 64 / k; i++) {
			ptr = (cacheline + i * k);
			if (k == 2)
				data_vec.push_back((uint64_t) (*(uint16_t *) ptr));
			else if (k == 4)
				data_vec.push_back((uint64_t) (*(uint32_t *) ptr));
			else if (k == 8)
				data_vec.push_back((uint64_t) (*(uint64_t *) ptr));
		}

		int64_t delta_base = 0;
		int64_t delta_immd = 0;
		int64_t immd = 0;

		//calculate the delta
		int compressed_size = 0;

		for (auto &base:data_vec) {
			delta_immd_vector.clear();
			delta_base_vector.clear();
			delta_flag.clear();
			base_id = 0;

			int max_delta_size_immd = 0;
			int max_delta_size_base = 0;

			//calculate delta
			for (auto &data:data_vec) {

				delta_base = data - base;
				delta_immd = data - immd;

				if (llabs(delta_base) >= llabs(delta_immd)) {
					int size = getDataSize(delta_immd);
					if (size > 64)
						break;

					if (max_delta_size_immd < size)
						max_delta_size_immd = size;

					delta_immd_vector.push_back(delta_immd);
					delta_flag.push_back(0);

				} else {
					int size = getDataSize(delta_base);
					if (size > 64)
						break;

					if (max_delta_size_base < size)
						max_delta_size_base = size;

					delta_base_vector.push_back(delta_base);
					delta_flag.push_back(1);
				}
			}

			//calculate compressed size
			compressed_size =
					k * 8 + delta_base_vector.size() * max_delta_size_base +
					delta_immd_vector.size() * max_delta_size_immd +
					(64 / k) + 6 * 2 + 2; //bits
			// compressed_size = base + delta@base + delta@immd
			//                   + delta_flag       //indicate base
			//                   + delta_size
			//                   + base_size        // (2B or 4B or 8B)

			//	output->verbose(CALL_INFO, 5, 0,
			//					"k:%d base: %llx max_delta_size_base: %d max_delta_size_immd: %d num_delta_base:%d num_delta_immd:%d compressed_size:%d compression ratio:%lf\n",
			//					k, base, max_delta_size_base, max_delta_size_immd, delta_base_vector.size(),
			//					delta_immd_vector.size(), compressed_size, (double) 512 / (double) compressed_size);


			//get min compressed size
			if (compressed_size < min_compressed_size) {
				min_k = k;
				min_base = base;
				min_compressed_size = compressed_size;

				//validate compression algorithm
				/*if (1) {
                        int base_idx = 0;
                        int immd_idx = 0;
                        int data_idx = 0;
                        uint64_t delta = 0;

                        std::vector<uint64_t> decompressed_data;

                        for (auto &flag:delta_flag) {
                            uint64_t data;
                            if (flag == 0) {
                                delta = delta_immd_vector[immd_idx++];
                                data = delta;
                            } else {
                                delta = delta_base_vector[base_idx++];

                                data = base + getSignedExtension(delta, getDataSize(delta));
                            }
                            decompressed_data.push_back(data);
                            //output->verbose(CALL_INFO, 5, 0, "data: %llx decompressed data: %llx\n",
                                            data_vec[data_idx], data);
                            if (data != data_vec[data_idx++]) {
                                printf("decompression error\n");
                                exit(1);
                            }
                        }
                    }*/
			}
		}
	}
	//output->verbose(CALL_INFO, 5, 0,
	//				"[CompressionResult] k:%d base: %llx compressed_size:%d compression ratio:%lf\n",
	//min_k, min_base, min_compressed_size, (double) 512 / (double) min_compressed_size);

	return min_compressed_size;
}



VOID checkSimilarity(uint64_t* data, uint64_t addr)
{
	int cacheline_addr=(addr>>6)<<6;
	int compressed_size=getCompressedSize((uint8_t*)data);
	bool compressible=false;

	if(compressed_size<=256)
		compressible = true;

	bool neighbor_compressible=false;

	char* tmp = (char*) malloc(sizeof(char)*64);
	int cnt=0;
	int l_similarity_comp_cnt=0;
	int l_similarity_incomp_cnt=0;

	g_accessed_cacheline_num++;

	//fprintf(stderr,"[PINTOOL] compressed size: %d\n",compressed_size);

	uint64_t neighbor_addr=addr+9;
	uint64_t page_num=(addr>>12);

	for(int i=0;i<similarity_distance;i++)
	{

		uint64_t neighbor_page=(neighbor_addr>>12);

		//fprintf(stderr,"[PINTOOL] page_num: %lld neigh pae_nu:%lld\n", page_num, neighbor_page);
		if(page_num!=neighbor_page)
			break;

		//ReadCacheLine(neighbor_addr,(uint64_t*)tmp);
		int neighbor_compressed_size=getCompressedSize((uint8_t*) tmp);
		if(compressible) {
			if (neighbor_compressed_size <= 256)
				l_similarity_comp_cnt++;
		}
		else {
			if (neighbor_compressed_size > 256)
				l_similarity_incomp_cnt++;
		}

		if(g_accessed_cacheline_num%1000000==0)
			fprintf(stderr,"[PINTOOL]distance: %d addr: %llx neighbor address:%llx neighbor compressed size: %d l_similarity_comp_cnt:%lld l_similarity_incomp_cnt:%lld accessed_cnt:%lld\n",
				similarity_distance, addr, neighbor_addr, neighbor_compressed_size, l_similarity_comp_cnt,l_similarity_incomp_cnt,g_accessed_cacheline_num);

		neighbor_addr+=8;
	}

	if(l_similarity_comp_cnt==similarity_distance)
		g_similarity_comp_cnt++;

	if(l_similarity_incomp_cnt==similarity_distance)
		g_similarity_incomp_cnt++;

	if(g_accessed_cacheline_num%1000000==0)
		fprintf(stderr,"[PINTOOL] g_similarity_cmp_cnt:%lld g_similarity_incomp_cnt:%lld accessed_cnt:%lld\n", g_similarity_comp_cnt,g_similarity_incomp_cnt,g_accessed_cacheline_num);

	free(tmp);
}

std::vector<std::vector<StackRecord> > arielStack; // Per-thread stacks

/* Instrumentation function to be called on function calls */
VOID ariel_stack_call(THREADID thr, ADDRINT stackPtr, ADDRINT target, ADDRINT ip) {
    // Handle longjmp
    while (arielStack[thr].size() > 0 && stackPtr >= arielStack[thr].back().getStackPtr()) {
        //fprintf(btfiles[thr], "RET ON CALL %s (0x%" PRIx64 ", 0x%" PRIx64 ")\n", RTN_FindNameByAddress(arielStack[thr].back().getTarget()).c_str(), arielStack[thr].back().getInstPtr(), arielStack[thr].back().getStackPtr());
        arielStack[thr].pop_back();
    }
    // Add new record
    arielStack[thr].push_back(StackRecord(stackPtr, target, ip));
    //fprintf(btfiles[thr], "CALL %s (0x%" PRIx64 ", 0x%" PRIx64 ")\n", RTN_FindNameByAddress(target).c_str(), ip, stackPtr);
}

/* Instrumentation function to be called on function returns */
VOID ariel_stack_return(THREADID thr, ADDRINT stackPtr) {
    // Handle longjmp
    while (arielStack[thr].size() > 0 && stackPtr >= arielStack[thr].back().getStackPtr()) {
        //fprintf(btfiles[thr], "RET ON RET %s (0x%" PRIx64 ", 0x%" PRIx64 ")\n", RTN_FindNameByAddress(arielStack[thr].back().getTarget()).c_str(), arielStack[thr].back().getInstPtr(), arielStack[thr].back().getStackPtr());
        arielStack[thr].pop_back();
    }
    // Remove last record
    //fprintf(btfiles[thr], "RET %s (0x%" PRIx64 ", 0x%" PRIx64 ")\n", RTN_FindNameByAddress(arielStack[thr].back().getTarget()).c_str(), arielStack[thr].back().getInstPtr(), arielStack[thr].back().getStackPtr());
    arielStack[thr].pop_back();
}

/* Function to print stack, called by malloc instrumentation code */
VOID ariel_print_stack(UINT32 thr, UINT64 allocSize, UINT64 allocAddr, UINT64 allocIndex) {

    unsigned int depth = arielStack[thr].size() - 1;
    BT_PRINTF("Malloc,0x%" PRIx64 ",%lu,%" PRIu64 "\n", allocAddr, allocSize, allocIndex);

    vector<ADDRINT> newMappings;
    for (vector<StackRecord>::reverse_iterator rit = arielStack[thr].rbegin(); rit != arielStack[thr].rend(); rit++) {

        // Note this only works if app is compiled with debug on
        if (instPtrsList[thr].find(rit->getInstPtr()) == instPtrsList[thr].end()) {
            newMappings.push_back(rit->getInstPtr());
            instPtrsList[thr].insert(rit->getInstPtr());
        }

        BT_PRINTF("0x%" PRIx64 ",0x%" PRIx64 ",%s", rit->getTarget(), rit->getInstPtr(), ((depth == 0) ? "\n" : ""));
        depth--;
    }
    // Generate any new mappings
    for (std::vector<ADDRINT>::iterator it = newMappings.begin(); it != newMappings.end(); it++) {
        string file;
        int line;
        PIN_LockClient();
            PIN_GetSourceLocation(*it, NULL, &line, &file);
        PIN_UnlockClient();
        BT_PRINTF("MAP: 0x%" PRIx64 ", %s:%d\n", *it, file.c_str(), line);

    }
}

/* Instrument traces to pick up calls and returns */
VOID InstrumentTrace (TRACE trace, VOID* args) {
    // For checking for jumps into shared libraries
    RTN rtn = TRACE_Rtn(trace);

    // Check each basic block tail
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        INS tail = BBL_InsTail(bbl);

        if (INS_IsCall(tail)) {
            if (INS_IsDirectBranchOrCall(tail)) {
                ADDRINT target = INS_DirectBranchOrCallTargetAddress(tail);
                INS_InsertPredicatedCall(tail, IPOINT_BEFORE, (AFUNPTR)
                    ariel_stack_call,
                    IARG_THREAD_ID,
                    IARG_REG_VALUE, REG_STACK_PTR,
                    IARG_ADDRINT, target,
		    IARG_INST_PTR,
                    IARG_END);
            } else if (!RTN_Valid(rtn) || ".plt" != SEC_Name(RTN_Sec(rtn))) {
                INS_InsertPredicatedCall(tail, IPOINT_BEFORE,
                    (AFUNPTR) ariel_stack_call,
                    IARG_THREAD_ID,
                    IARG_REG_VALUE, REG_STACK_PTR,
                    IARG_BRANCH_TARGET_ADDR,
		    IARG_INST_PTR,
                    IARG_END);
            }

        }
        if (RTN_Valid(rtn) && ".plt" == SEC_Name(RTN_Sec(rtn))) {
            INS_InsertCall(tail, IPOINT_BEFORE, (AFUNPTR)
                ariel_stack_call,
                    IARG_THREAD_ID,
                    IARG_REG_VALUE, REG_STACK_PTR,
                    IARG_BRANCH_TARGET_ADDR,
		    IARG_INST_PTR,
                    IARG_END);
        }
        if (INS_IsRet(tail)) {
            INS_InsertPredicatedCall(tail, IPOINT_BEFORE, (AFUNPTR)
                    ariel_stack_return,
                    IARG_THREAD_ID,
                    IARG_REG_VALUE, REG_STACK_PTR,
                    IARG_END);
        }
    }
}
int64_t getSignedExtension(int64_t data, uint32_t size) {
	assert(size<=65);
	int64_t new_data;
	new_data=(data<<(64-size))>>(64-size);
	return new_data;
}




/****************************************************************/
/******************** END SHADOW STACK **************************/
/****************************************************************/

VOID Fini(INT32 code, VOID* v)
{
	if(SSTVerbosity.Value() > 0) {
		std::cout << "SSTARIEL: Execution completed, shutting down." << std::endl;
	}

		if(similarity_distance>0)
	{
		fprintf(stderr,"g_similarity_comp_cnt:%lld", g_similarity_comp_cnt);
		fprintf(stderr,"g_similarity_incomp_cnt:%lld", g_similarity_incomp_cnt);
		fprintf(stderr,"accessed_cacheline_num:%lld", g_accessed_cacheline_num);
		fprintf(stderr,"simularity:%f", (g_similarity_incomp_cnt+g_similarity_comp_cnt)/g_accessed_cacheline_num);

	}

    ArielCommand ac;
    ac.command = ARIEL_PERFORM_EXIT;
    ac.instPtr = (uint64_t) 0;
    tunnel->writeMessage(0, ac);

    delete tunnel;


    if(funcProfileLevel > 0) {
    	FILE* funcProfileOutput = fopen("func.profile", "wt");

    	for(std::map<std::string, ArielFunctionRecord*>::iterator funcItr = funcProfile.begin();
    			funcItr != funcProfile.end(); funcItr++) {
    		fprintf(funcProfileOutput, "%s %" PRId64 "\n", funcItr->first.c_str(),
    			funcItr->second->insExecuted);
    	}

    	fclose(funcProfileOutput);
    }

    // Close backtrace files if needed
    if (KeepMallocStackTrace.Value() == 1) {
        fclose(rtnNameMap);
        for (int i = 0; i < core_count; i++) {
            if (btfiles[i] != NULL)
#ifdef HAVE_LIBZ
                gzclose(btfiles[i]);
#else
                fclose(btfiles[i]);
#endif
        }
    }
}



VOID copy(void* dest, const void* input, UINT32 length) {
	for(UINT32 i = 0; i < length; ++i) {
		((char*) dest)[i] = ((char*) input)[i];
	}
}

VOID RecordAddrSize(THREADID thr, VOID* raddr, UINT32 rsize, VOID* waddr, UINT32 wsize, ADDRINT ip)
{
    thread_instr_id[thr].raddr=(UINT64)raddr;
    thread_instr_id[thr].waddr=(UINT64)waddr;
    thread_instr_id[thr].rsize=rsize;
    thread_instr_id[thr].wsize=wsize;
    thread_instr_id[thr].ip=ip;
 //   printf("addrsize: addr:%lld size:%ld\n",addr,size);
}





VOID WriteInstructionRead(UINT64 addr, UINT32 size, THREADID thr, ADDRINT ip,
	UINT32 instClass, UINT32 simdOpWidth) {

	inst_cnt++;

	if(inst_cnt<warmup_insts) {
		enable_output = false;
		//enable_output = true;
	}
	else if(inst_cnt==warmup_insts) {
		fprintf(stderr,"warmup inst_cnt[%lld]\n", inst_cnt);
		enable_output = true;
	}else if(inst_cnt>warmup_insts)
	{

		enable_output = true;
	}


        if(thread_instr_id[thr].ip!=ip)
        {
            fprintf(stderr,"ip is mismatch\n");
            exit(-1);
        }


        ArielCommand ac;

        ac.command = ARIEL_PERFORM_READ;
	ac.instPtr = (uint64_t) ip;
        ac.inst.addr = addr;
        ac.inst.size = size;
	ac.inst.instClass = instClass;
	ac.inst.simdElemCount = simdOpWidth;

        //assume that cache line size is 64B

		if(content_copy_en) {
			uint64_t *data = (uint64_t *) malloc(sizeof(uint64_t) * 8);
			ReadCacheLine(addr, data);
			for (int i = 0; i < 8; i++) {
				ac.inst.data[i] = *(data + i);
#ifdef COMP_DEBUG
				fprintf(stderr,"[PINTOOL] [%d] read addr:%llx data:%llx ac.inst.data:%llx\n", i, addr, *(data+i), ac.inst.data[i]);
#endif

			}
			if(similarity_distance>0)
				checkSimilarity((uint64_t*)ac.inst.data,ac.inst.addr);
			free(data);
		}


        //std::cout<<"addr: 0x"<<std::hex<<addr64
        //    <<"data: 0x"<<data<<" "
        //    <<"copied size: "<<copied_size<<std::endl;



        tunnel->writeMessage(thr, ac);
}

VOID WriteInstructionWrite(UINT64 addr, UINT32 size, THREADID thr, ADDRINT ip,
	UINT32 instClass, UINT32 simdOpWidth) {

	inst_cnt++;


	if(inst_cnt<warmup_insts) {
		enable_output = false;
		//enable_output=true;
	}
	else if(inst_cnt==warmup_insts) {
		fprintf(stderr,"warmup inst_cnt[%lld]\n", inst_cnt);
		enable_output = true;
	}else if(inst_cnt>warmup_insts)
	{
		enable_output = true;
	}


        if(thread_instr_id[thr].ip!=ip)
        {
            fprintf(stderr,"ip is mismatch\n");
            exit(-1);
        }


        if(thread_instr_id[thr].ip!=ip){
            fprintf(stderr,"ip is mismatch\n");
            exit(-1);
        }


        ArielCommand ac;

        ac.command = ARIEL_PERFORM_WRITE;
	    ac.instPtr = (uint64_t) ip;
        ac.inst.addr = addr;
        ac.inst.size = size;
	    ac.inst.instClass = instClass;
        ac.inst.simdElemCount = simdOpWidth;

		if(content_copy_en) {
			//assume that cache line size is 64B
			uint64_t *data = (uint64_t *) malloc(sizeof(uint64_t) * 8);
			ReadCacheLine(addr, data);
			for (int i = 0; i < 8; i++) {
				ac.inst.data[i] = *(data + i);
//#ifdef COMP_DEBUG
//            fprintf(stderr,"[PINTOOL] [%d] write addr:%llx data:%llx ac.inst.data:%llx\n", i, addr, *(data+i), ac.inst.data[i]);
//#endif

			}

			checkSimilarity(data, addr);
			free(data);
		}

        tunnel->writeMessage(thr, ac);
}

VOID WriteStartInstructionMarker(UINT32 thr, ADDRINT ip) {
    	ArielCommand ac;
    	ac.command = ARIEL_START_INSTRUCTION;
    	ac.instPtr = (uint64_t) ip;
    	tunnel->writeMessage(thr, ac);
}

VOID WriteEndInstructionMarker(UINT32 thr, ADDRINT ip) {
    	ArielCommand ac;
    	ac.command = ARIEL_END_INSTRUCTION;
    	ac.instPtr = (uint64_t) ip;
    	tunnel->writeMessage(thr, ac);
}

VOID WriteInstructionReadWrite(THREADID thr, ADDRINT ip, UINT32 instClass,
	UINT32 simdOpWidth ) {

        const uint64_t readAddr=(uint64_t) thread_instr_id[thr].raddr;
        const uint32_t readSize=(uint32_t) thread_instr_id[thr].rsize;
        const uint64_t writeAddr=(uint64_t) thread_instr_id[thr].waddr;
        const uint32_t writeSize=(uint32_t) thread_instr_id[thr].wsize;
	if(enable_output) {
		if(thr < core_count) {
			WriteStartInstructionMarker( thr, ip );
			WriteInstructionRead(  readAddr,  readSize,  thr, ip, instClass, simdOpWidth );
			WriteInstructionWrite( writeAddr, writeSize, thr, ip, instClass, simdOpWidth );
			WriteEndInstructionMarker( thr, ip );
		}
	}
}

VOID WriteInstructionReadOnly(THREADID thr, ADDRINT ip,
	UINT32 instClass, UINT32 simdOpWidth) {
        const uint64_t readAddr=(uint64_t) thread_instr_id[thr].raddr;
        const uint32_t readSize=(uint32_t) thread_instr_id[thr].rsize;
       
        if(enable_output) {
		if(thr < core_count) {
			WriteStartInstructionMarker(thr, ip);
			WriteInstructionRead(  readAddr,  readSize,  thr, ip, instClass, simdOpWidth );
			WriteEndInstructionMarker(thr, ip);
		}
	}

}

VOID WriteNoOp(THREADID thr, ADDRINT ip) {
	inst_cnt++;

	if(inst_cnt<warmup_insts) {
		enable_output = false;
	}
	else if(inst_cnt==warmup_insts) {
		fprintf(stderr,"warmup inst_cnt[%lld]\n", inst_cnt);
		enable_output = true;
	}else if(inst_cnt>warmup_insts)
	{
		enable_output = true;
	}

	if(enable_output) {
		if(thr < core_count) {
            		ArielCommand ac;
            		ac.command = ARIEL_NOOP;
            		ac.instPtr = (uint64_t) ip;
            		tunnel->writeMessage(thr, ac);
		}
	}
}

VOID WriteInstructionWriteOnly(THREADID thr, ADDRINT ip,
	UINT32 instClass, UINT32 simdOpWidth) {
         const uint64_t writeAddr=(uint64_t) thread_instr_id[thr].waddr;
        const uint32_t writeSize=(uint32_t) thread_instr_id[thr].wsize;
       

	if(enable_output) {
		if(thr < core_count) {
                        WriteStartInstructionMarker(thr, ip);
                        WriteInstructionWrite(writeAddr, writeSize,  thr, ip, instClass, simdOpWidth);
                        WriteEndInstructionMarker(thr, ip);
		}
	}

}

VOID IncrementFunctionRecord(VOID* funcRecord) {
	ArielFunctionRecord* arielFuncRec = (ArielFunctionRecord*) funcRecord;

	__asm__ __volatile__(
	    "lock incq %0"
	     : /* no output registers */
	     : "m" (arielFuncRec->insExecuted)
	     : "memory"
	);
}

VOID InstrumentInstruction(INS ins, VOID *v)
{
	UINT32 simdOpWidth     = 1;
	UINT32 instClass       = ARIEL_INST_UNKNOWN;
	UINT32 maxSIMDRegWidth = 1;


		std::string instCode = INS_Mnemonic(ins);

		for (UINT32 i = 0; i < INS_MaxNumRRegs(ins); i++) {
			if (REG_is_xmm(INS_RegR(ins, i))) {
				maxSIMDRegWidth = ARIEL_MAX(maxSIMDRegWidth, 2);
			} else if (REG_is_ymm(INS_RegR(ins, i))) {
				maxSIMDRegWidth = ARIEL_MAX(maxSIMDRegWidth, 4);
			} else if (REG_is_zmm(INS_RegR(ins, i))) {
				maxSIMDRegWidth = ARIEL_MAX(maxSIMDRegWidth, 8);
			}
		}

		for (UINT32 i = 0; i < INS_MaxNumWRegs(ins); i++) {
			if (REG_is_xmm(INS_RegW(ins, i))) {
				maxSIMDRegWidth = ARIEL_MAX(maxSIMDRegWidth, 2);
			} else if (REG_is_ymm(INS_RegW(ins, i))) {
				maxSIMDRegWidth = ARIEL_MAX(maxSIMDRegWidth, 4);
			} else if (REG_is_zmm(INS_RegW(ins, i))) {
				maxSIMDRegWidth = ARIEL_MAX(maxSIMDRegWidth, 8);
			}
		}

		if (instCode.size() > 1) {
			std::string prefix = "";

			if (instCode.size() > 2) {
				prefix = instCode.substr(0, 3);
			}

			std::string suffix = instCode.substr(instCode.size() - 2);

			if ("MOV" == prefix || "mov" == prefix) {
				// Do not found MOV as an FP instruction?
				simdOpWidth = 1;
			} else {
				if ((suffix == "PD") || (suffix == "pd")) {
					simdOpWidth = maxSIMDRegWidth;
					instClass = ARIEL_INST_DP_FP;
				} else if ((suffix == "PS") || (suffix == "ps")) {
					simdOpWidth = maxSIMDRegWidth * 2;
					instClass = ARIEL_INST_SP_FP;
				} else if ((suffix == "SD") || (suffix == "sd")) {
					simdOpWidth = 1;
					instClass = ARIEL_INST_DP_FP;
				} else if ((suffix == "SS") || (suffix == "ss")) {
					simdOpWidth = 1;
					instClass = ARIEL_INST_SP_FP;
				} else {
					simdOpWidth = 1;
				}
			}
		}

		if (INS_IsMemoryRead(ins) && INS_IsMemoryWrite(ins)) {
			INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)
											 RecordAddrSize,
									 IARG_THREAD_ID,
									 IARG_MEMORYREAD_EA, IARG_UINT32, INS_MemoryReadSize(ins),
									 IARG_MEMORYWRITE_EA, IARG_UINT32, INS_MemoryWriteSize(ins),
									 IARG_INST_PTR,
									 IARG_END);

			if (INS_HasFallThrough(ins)) {
				INS_InsertPredicatedCall(ins, IPOINT_AFTER, (AFUNPTR)
												 WriteInstructionReadWrite,
										 IARG_THREAD_ID,
										 IARG_INST_PTR,
										 IARG_UINT32, instClass,
										 IARG_UINT32, simdOpWidth,
										 IARG_END);
			} else if (INS_IsBranchOrCall(ins)) {
				INS_InsertPredicatedCall(ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)
												 WriteInstructionReadWrite,
										 IARG_THREAD_ID,
										 IARG_INST_PTR,
										 IARG_UINT32, instClass,
										 IARG_UINT32, simdOpWidth,
										 IARG_END);
			}

		} else if (INS_IsMemoryRead(ins)) {
			INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)
											 RecordAddrSize,
									 IARG_THREAD_ID,
									 IARG_MEMORYREAD_EA, IARG_UINT32, INS_MemoryReadSize(ins),
									 IARG_MEMORYREAD_EA, IARG_UINT32, INS_MemoryReadSize(ins),
									 IARG_INST_PTR,
									 IARG_END);

			if (INS_HasFallThrough(ins))
				INS_InsertPredicatedCall(ins, IPOINT_AFTER, (AFUNPTR)
												 WriteInstructionReadOnly,
										 IARG_THREAD_ID,
										 IARG_INST_PTR,
										 IARG_UINT32, instClass,
										 IARG_UINT32, simdOpWidth,
										 IARG_END);

			else if (INS_IsBranchOrCall(ins))
				INS_InsertPredicatedCall(ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)
												 WriteInstructionReadOnly,
										 IARG_THREAD_ID,
										 IARG_INST_PTR,
										 IARG_UINT32, instClass,
										 IARG_UINT32, simdOpWidth,
										 IARG_END);
		} else if (INS_IsMemoryWrite(ins)) {
			INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)
											 RecordAddrSize,
									 IARG_THREAD_ID,
									 IARG_MEMORYWRITE_EA, IARG_UINT32, INS_MemoryWriteSize(ins),
									 IARG_MEMORYWRITE_EA, IARG_UINT32, INS_MemoryWriteSize(ins),
									 IARG_INST_PTR,
									 IARG_END);

			if (INS_HasFallThrough(ins))
				INS_InsertPredicatedCall(ins, IPOINT_AFTER, (AFUNPTR)
												 WriteInstructionWriteOnly,
										 IARG_THREAD_ID,
										 IARG_INST_PTR,
										 IARG_UINT32, instClass,
										 IARG_UINT32, simdOpWidth,
										 IARG_END);
			else if (INS_IsBranchOrCall(ins))
				INS_InsertPredicatedCall(ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)
												 WriteInstructionWriteOnly,
										 IARG_THREAD_ID,
										 IARG_INST_PTR,
										 IARG_UINT32, instClass,
										 IARG_UINT32, simdOpWidth,
										 IARG_END);

		} else {
			INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)
											 WriteNoOp,
									 IARG_THREAD_ID,
									 IARG_INST_PTR,
									 IARG_END);
		}

		if (funcProfileLevel > 0) {
			RTN rtn = INS_Rtn(ins);
			std::string rtn_name = "Unknown Function";

			if (RTN_Valid(rtn)) {
				rtn_name = RTN_Name(rtn);
			}

			std::map<std::string, ArielFunctionRecord *>::iterator checkExists =
					funcProfile.find(rtn_name);
			ArielFunctionRecord *funcRecord = NULL;

			if (checkExists == funcProfile.end()) {
				funcRecord = new ArielFunctionRecord();
				funcProfile.insert(std::pair<std::string, ArielFunctionRecord *>(rtn_name,
																				 funcRecord));
			} else {
				funcRecord = checkExists->second;
			}

			INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) IncrementFunctionRecord,
									 IARG_PTR, (void *) funcRecord, IARG_END);
		}

}

void mapped_ariel_enable() {
    fprintf(stderr, "ARIEL: Enabling memory and instruction tracing from program control.\n");
    fflush(stdout);
    fflush(stderr);
    enable_output = true;
}

uint64_t mapped_ariel_cycles() {
    return tunnel->getCycles();
}

int mapped_gettimeofday(struct timeval *tp, void *tzp) {
    if ( tp == NULL ) { errno = EINVAL ; return -1; }

    tunnel->getTime(tp);
    return 0;
}

void mapped_ariel_output_stats() {
    THREADID thr = PIN_ThreadId();
    ArielCommand ac;
    ac.command = ARIEL_OUTPUT_STATS;
    ac.instPtr = (uint64_t) 0;
    tunnel->writeMessage(thr, ac);
}

// same effect as mapped_ariel_output_stats(), but it also sends a user-defined reference number back
void mapped_ariel_output_stats_buoy(uint64_t marker) {
    THREADID thr = PIN_ThreadId();
    ArielCommand ac;
    ac.command = ARIEL_OUTPUT_STATS;
    ac.instPtr = (uint64_t) marker; //user the instruction pointer slot to send the marker number
    tunnel->writeMessage(thr, ac);
}

#if ! defined(__APPLE__)
int mapped_clockgettime(clockid_t clock, struct timespec *tp) {
    if (tp == NULL) { errno = EINVAL; return -1; }
    tunnel->getTimeNs(tp);
    return 0;
}
#endif

int ariel_mlm_memcpy(void* dest, void* source, size_t size) {
#ifdef ARIEL_DEBUG
	fprintf(stderr, "Perform a mlm_memcpy from Ariel from %p to %p length %llu\n",
		source, dest, size);
#endif

	char* dest_c = (char*) dest;
	char* src_c  = (char*) source;

	// Perform the memory copy on behalf of the application
	for(size_t i = 0; i < size; ++i) {
		dest_c[i] = src_c[i];
	}

	THREADID currentThread = PIN_ThreadId();
	UINT32 thr = (UINT32) currentThread;

	if(thr >= core_count) {
		fprintf(stderr, "Thread ID: %" PRIu32 " is greater than core count.\n", thr);
		exit(-4);
	}

    const uint64_t ariel_src     = (uint64_t) source;
    const uint64_t ariel_dest    = (uint64_t) dest;
    const uint32_t length        = (uint32_t) size;

    ArielCommand ac;
    ac.command = ARIEL_START_DMA;
    ac.dma_start.src = ariel_src;
    ac.dma_start.dest = ariel_dest;
    ac.dma_start.len = length;

    tunnel->writeMessage(thr, ac);

#ifdef ARIEL_DEBUG
	fprintf(stderr, "Done with ariel memcpy.\n");
#endif

	return 0;
}

void ariel_mlm_set_pool(int new_pool) {
#ifdef ARIEL_DEBUG
	fprintf(stderr, "Ariel perform a mlm_switch_pool to level %d\n", new_pool);
#endif

    THREADID currentThread = PIN_ThreadId();
    UINT32 thr = (UINT32) currentThread;

#ifdef ARIEL_DEBUG
    fprintf(stderr, "Requested: %llu, but expanded to: %llu (on thread: %lu) \n", size, real_req_size,
            thr);
#endif

    const uint32_t newDefaultPool = (uint32_t) new_pool;

    ArielCommand ac;
    ac.command = ARIEL_SWITCH_POOL;
    ac.switchPool.pool = newDefaultPool;
    tunnel->writeMessage(thr, ac);

	// Keep track of the default pool
	default_pool = (UINT32) new_pool;
}

void* ariel_mlm_malloc(size_t size, int level) {
    THREADID currentThread = PIN_ThreadId();
    UINT32 thr = (UINT32) currentThread;

#ifdef ARIEL_DEBUG
    fprintf(stderr, "%u, Perform a mlm_malloc from Ariel %zu, level %d\n", thr, size, level);
#endif
    if(0 == size) {
        fprintf(stderr, "YOU REQUESTED ZERO BYTES\n");
        void *bt_entries[64];
        int entry_returned = backtrace(bt_entries, 64);
	backtrace_symbols(bt_entries, entry_returned);
        exit(-8);
    }

    size_t page_diff = size % ((size_t)4096);
    size_t npages = size / ((size_t)4096);

    size_t real_req_size = 4096 * (npages + ((page_diff == 0) ? 0 : 1));

#ifdef ARIEL_DEBUG
    fprintf(stderr, "Requested: %llu, but expanded to: %llu (on thread: %lu) \n",
            size, real_req_size, thr);
#endif

    void* real_ptr = 0;
    posix_memalign(&real_ptr, 4096, real_req_size);

    const uint64_t virtualAddress = (uint64_t) real_ptr;
    const uint64_t allocationLength = (uint64_t) real_req_size;
    const uint32_t allocationLevel = (uint32_t) level;

    ArielCommand ac;
    ac.command = ARIEL_ISSUE_TLM_MAP;
    ac.mlm_map.vaddr = virtualAddress;
    ac.mlm_map.alloc_len = allocationLength;

    if(shouldOverride) {
        ac.mlm_map.alloc_level = overridePool;
    } else {
        ac.mlm_map.alloc_level = allocationLevel;
    }

    tunnel->writeMessage(thr, ac);

#ifdef ARIEL_DEBUG
    fprintf(stderr, "%u: Ariel mlm_malloc call allocates data at address: 0x%llx\n",
            thr, (uint64_t) real_ptr);
#endif

    PIN_GetLock(&mainLock, thr);
	allocated_list.push_back(real_ptr);
    PIN_ReleaseLock(&mainLock);
	return real_ptr;
}

void ariel_mlm_free(void* ptr) {
	THREADID currentThread = PIN_ThreadId();
	UINT32 thr = (UINT32) currentThread;

#ifdef ARIEL_DEBUG
	fprintf(stderr, "Perform a mlm_free from Ariel (pointer = %p) on thread %lu\n", ptr, thr);
#endif

	bool found = false;
	std::vector<void*>::iterator ptr_list_itr;
    PIN_GetLock(&mainLock, thr);
	for(ptr_list_itr = allocated_list.begin(); ptr_list_itr != allocated_list.end(); ptr_list_itr++) {
		if(*ptr_list_itr == ptr) {
			found = true;
			allocated_list.erase(ptr_list_itr);
			break;
		}
	}
    PIN_ReleaseLock(&mainLock);

	if(found) {
#ifdef ARIEL_DEBUG
		fprintf(stderr, "ARIEL: Matched call to free, passing to Ariel free routine.\n");
#endif
		free(ptr);

		const uint64_t virtAddr = (uint64_t) ptr;

        ArielCommand ac;
        ac.command = ARIEL_ISSUE_TLM_FREE;
        ac.mlm_free.vaddr = virtAddr;
        tunnel->writeMessage(thr, ac);

	} else {
		fprintf(stderr, "ARIEL: Call to free in Ariel did not find a matching local allocation, this memory will be leaked.\n");
	}
}

VOID ariel_premalloc_instrument(ADDRINT allocSize, ADDRINT ip) {
		THREADID currentThread = PIN_ThreadId();
		UINT32 thr = (UINT32) currentThread;

        lastMallocSize[thr] = (UINT64) allocSize;
        lastMallocLoc[thr] = (UINT64) ip;
}

VOID ariel_postmalloc_instrument(ADDRINT allocLocation) {
    if(lastMallocSize != NULL) {
        THREADID currentThread = PIN_ThreadId();
        UINT32 thr = (UINT32) currentThread;
		
        const uint64_t virtualAddress = (uint64_t) allocLocation;
        const uint64_t allocationLength = (uint64_t) lastMallocSize[thr];
        const uint32_t allocationLevel = (uint32_t) default_pool;

        uint64_t myIndex = 0;
        // Dump stack if we need it
        if (KeepMallocStackTrace.Value() == 1) {
            PIN_GetLock(&mallocIndexLock, thr);
            myIndex = mallocIndex;
            mallocIndex++;
            PIN_ReleaseLock(&mallocIndexLock);
            ariel_print_stack(thr, allocationLength, allocLocation, myIndex);
        }
        ArielCommand ac;
        ac.command = ARIEL_ISSUE_TLM_MAP;
        ac.instPtr = myIndex;
        ac.mlm_map.vaddr = virtualAddress;
        ac.mlm_map.alloc_len = allocationLength;


        if(shouldOverride) {
            ac.mlm_map.alloc_level = overridePool;
        } else {
            ac.mlm_map.alloc_level = allocationLevel;
        }
        tunnel->writeMessage(thr, ac);
        
    	/*printf("ARIEL: Created a malloc of size: %" PRIu64 " in Ariel\n",
         * (UINT64) allocationLength);*/
    }
}

VOID ariel_postfree_instrument(ADDRINT allocLocation) {
	THREADID currentThread = PIN_ThreadId();
	UINT32 thr = (UINT32) currentThread;

	const uint64_t virtAddr = (uint64_t) allocLocation;

    ArielCommand ac;
    ac.command = ARIEL_ISSUE_TLM_FREE;
    ac.mlm_free.vaddr = virtAddr;
    tunnel->writeMessage(thr, ac);
}

VOID InstrumentRoutine(RTN rtn, VOID* args) {
    if (KeepMallocStackTrace.Value() == 1) {
        fprintf(rtnNameMap, "0x%" PRIx64 ", %s\n", RTN_Address(rtn), RTN_Name(rtn).c_str());   

    }

    if (RTN_Name(rtn) == "ariel_enable" || RTN_Name(rtn) == "_ariel_enable" || RTN_Name(rtn) == "__arielfort_MOD_ariel_enable") {
        fprintf(stderr,"Identified routine: ariel_enable, replacing with Ariel equivalent...\n");
        RTN_Replace(rtn, (AFUNPTR) mapped_ariel_enable);
        fprintf(stderr,"Replacement complete.\n");
        if (StartupMode.Value() == 2) {
            fprintf(stderr, "Tool was called with auto-detect enable mode, setting initial output to not be traced.\n");
            enable_output = false;
        }
        return;
    } else if (RTN_Name(rtn) == "gettimeofday" || RTN_Name(rtn) == "_gettimeofday") {
        fprintf(stderr,"Identified routine: gettimeofday, replacing with Ariel equivalent...\n");
        RTN_Replace(rtn, (AFUNPTR) mapped_gettimeofday);
        fprintf(stderr,"Replacement complete.\n");
        return;
    } else if (RTN_Name(rtn) == "ariel_cycles" || RTN_Name(rtn) == "_ariel_cycles") {
        fprintf(stderr, "Identified routine: ariel_cycles, replacing with Ariel equivalent..\n");
        RTN_Replace(rtn, (AFUNPTR) mapped_ariel_cycles);
        fprintf(stderr, "Replacement complete\n");
        return;
#if ! defined(__APPLE__)
    } else if (RTN_Name(rtn) == "clock_gettime" || RTN_Name(rtn) == "_clock_gettime" ||
        RTN_Name(rtn) == "__clock_gettime") {
        fprintf(stderr,"Identified routine: clock_gettime, replacing with Ariel equivalent...\n");
        RTN_Replace(rtn, (AFUNPTR) mapped_clockgettime);
        fprintf(stderr,"Replacement complete.\n");
        return;
#endif
    } else if ((InterceptMultiLevelMemory.Value() > 0) && RTN_Name(rtn) == "mlm_malloc") {
        // This means we want a special malloc to be used (needs a TLB map inside the virtual core)
        fprintf(stderr,"Identified routine: mlm_malloc, replacing with Ariel equivalent...\n");
        AFUNPTR ret = RTN_Replace(rtn, (AFUNPTR) ariel_mlm_malloc);
        fprintf(stderr,"Replacement complete. (%p)\n", ret);
        return;
    } else if ((InterceptMultiLevelMemory.Value() > 0) && RTN_Name(rtn) == "mlm_free") {
        fprintf(stderr,"Identified routine: mlm_free, replacing with Ariel equivalent...\n");
        RTN_Replace(rtn, (AFUNPTR) ariel_mlm_free);
        fprintf(stderr, "Replacement complete.\n");
        return;
    } else if ((InterceptMultiLevelMemory.Value() > 0) && RTN_Name(rtn) == "mlm_set_pool") {
        fprintf(stderr, "Identified routine: mlm_set_pool, replacing with Ariel equivalent...\n");
        RTN_Replace(rtn, (AFUNPTR) ariel_mlm_set_pool);
        fprintf(stderr, "Replacement complete.\n");
        return;
    } else if ((InterceptMultiLevelMemory.Value() > 0) && (
                RTN_Name(rtn) == "malloc" || RTN_Name(rtn) == "_malloc" || RTN_Name(rtn) == "__libc_malloc" || RTN_Name(rtn) == "__libc_memalign" || RTN_Name(rtn) == "_gfortran_malloc")) {
    		
        fprintf(stderr, "Identified routine: malloc/_malloc, replacing with Ariel equivalent...\n");
        RTN_Open(rtn);

        RTN_InsertCall(rtn, IPOINT_BEFORE,
            (AFUNPTR) ariel_premalloc_instrument,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                IARG_INST_PTR, 
                IARG_END);

        RTN_InsertCall(rtn, IPOINT_AFTER,
                       (AFUNPTR) ariel_postmalloc_instrument,
                       IARG_FUNCRET_EXITPOINT_VALUE,
                       IARG_END);

        RTN_Close(rtn);
    } else if ((InterceptMultiLevelMemory.Value() > 0) && (
                RTN_Name(rtn) == "free" || RTN_Name(rtn) == "_free" || RTN_Name(rtn) == "__libc_free" || RTN_Name(rtn) == "_gfortran_free")) {

        fprintf(stderr, "Identified routine: free/_free, replacing with Ariel equivalent...\n");
        RTN_Open(rtn);

        RTN_InsertCall(rtn, IPOINT_BEFORE,
            (AFUNPTR) ariel_postfree_instrument,
                IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                IARG_END);

        RTN_Close(rtn);
    } else if (RTN_Name(rtn) == "ariel_output_stats" || RTN_Name(rtn) == "_ariel_output_stats" || RTN_Name(rtn) == "__arielfort_MOD_ariel_output_stats") {
        fprintf(stderr, "Identified routine: ariel_output_stats, replacing with Ariel equivalent..\n");
        RTN_Replace(rtn, (AFUNPTR) mapped_ariel_output_stats);
        fprintf(stderr, "Replacement complete\n");
        return;
    } else if (RTN_Name(rtn) == "ariel_output_stats_buoy" || RTN_Name(rtn) == "_ariel_output_stats_buoy") {
        fprintf(stderr, "Identified routine: ariel_output_stats_buoy, replacing with Ariel equivalent..\n");
        RTN_Replace(rtn, (AFUNPTR) mapped_ariel_output_stats_buoy);
        fprintf(stderr, "Replacement complete\n");
        return;
    }

}


/*(===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    PIN_ERROR( "This Pintool collects statistics for instructions.\n" 
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) return Usage();

    // Load the symbols ready for us to mangle functions.
    //PIN_InitSymbolsAlt(IFUNC_SYMBOLS);
    PIN_InitSymbols();
    PIN_AddFiniFunction(Fini, 0);

    PIN_InitLock(&mainLock);
    PIN_InitLock(&mallocIndexLock);

    if(SSTVerbosity.Value() > 0) {
        std::cout << "SSTARIEL: Loading Ariel Tool to connect to SST on pipe: " <<
            SSTNamedPipe.Value() << " max instruction count: " <<
            MaxInstructions.Value() << " warmup instruction count: " <<
            WarmupInstructions.Value() <<
            " max core count: " << MaxCoreCount.Value() << std::endl;
    }

    funcProfileLevel = TrapFunctionProfile.Value();

    if(funcProfileLevel > 0) {
    	std::cout << "SSTARIEL: Function profile level is configured to: " << funcProfileLevel << std::endl;
    } else {
    	std::cout << "SSTARIEL: Function profiling is disabled." << std::endl;
    }

    char* override_pool_name = getenv("ARIEL_OVERRIDE_POOL");
    if(NULL != override_pool_name) {
		fprintf(stderr, "ARIEL-SST: Override for memory pools\n");
		shouldOverride = true;
		overridePool = (UINT32) atoi(getenv("ARIEL_OVERRIDE_POOL"));
		fprintf(stderr, "ARIEL-SST: Use pool: %" PRIu32 " instead of application provided\n", overridePool);
    } else {
		fprintf(stderr, "ARIEL-SST: Did not find ARIEL_OVERRIDE_POOL in the environment, no override applies.\n");
    }

    core_count = MaxCoreCount.Value();

    tunnel = new ArielTunnel(SSTNamedPipe.Value());
    lastMallocSize = (UINT64*) malloc(sizeof(UINT64) * core_count);
    lastMallocLoc = (UINT64*) malloc(sizeof(UINT64) * core_count);
    mallocIndex = 0;

    if (KeepMallocStackTrace.Value() == 1) {
        arielStack.resize(core_count);  // Need core_count stacks
        rtnNameMap = fopen("routine_name_map.txt", "wt");
        instPtrsList.resize(core_count);    // Need core_count sets of instruction pointers (to avoid locks)
    }

    for(int i = 0; i < core_count; i++) {
    	lastMallocSize[i] = (UINT64) 0;
    	lastMallocLoc[i] = (UINT64) 0;
        
        // Shadow stack - open per-thread backtrace file
        if (KeepMallocStackTrace.Value() == 1) {
            stringstream fn;
            fn << "backtrace_" << i << ".txt";
            string filename(fn.str());
#ifdef HAVE_LIBZ
            filename += ".gz";
            btfiles.push_back(gzopen(filename.c_str(), "w"));
#else
            btfiles.push_back(fopen(filename.c_str(), "w"));
#endif
            if (btfiles.back() == NULL) {
                fprintf(stderr, "ARIEL ERROR: could not create and/or open backtrace file: %s\n", filename.c_str());
                return 73;  // EX_CANTCREATE
            }
        }
    }

    fprintf(stderr, "ARIEL-SST PIN tool activating with %" PRIu32 " threads\n", core_count);
    fflush(stdout);

    sleep(1);

    default_pool = DefaultMemoryPool.Value();
    fprintf(stderr, "ARIEL: Default memory pool set to %" PRIu32 "\n", default_pool);

    if(StartupMode.Value() == 1) {
        fprintf(stderr, "ARIEL: Tool is configured to begin with profiling immediately.\n");
        enable_output = true;
    } else if (StartupMode.Value() == 0) {
        fprintf(stderr, "ARIEL: Tool is configured to suspend profiling until program control\n");
        enable_output = false;
    } else if (StartupMode.Value() == 2) {
		fprintf(stderr, "ARIEL: Tool is configured to attempt auto detect of profiling\n");
		fprintf(stderr, "ARIEL: Initial mode will be to enable profiling unless ariel_enable function is located\n");
		enable_output = true;
    }

//    enable_memcomp = MemCompProfile.value();
 //   memcomp_tracefile = MemCompTraceName();
  //  memcomp_interval = MemCompTraceInterval();
    warmup_insts = WarmupInstructions.Value();
	similarity_distance = SimilarityDistance.Value();

	std::cout<<"warmup_insts: "<<warmup_insts<<std::endl;

    INS_AddInstrumentFunction(InstrumentInstruction, 0);
    RTN_AddInstrumentFunction(InstrumentRoutine, 0);
    
    // Instrument traces to capture stack
    if (KeepMallocStackTrace.Value() == 1)
        TRACE_AddInstrumentFunction(InstrumentTrace, 0);


    	int max_thread_count = 16;

	posix_memalign((void**) &thread_instr_id, 64, sizeof(threadRecord) * max_thread_count);


    fprintf(stderr, "ARIEL: Starting program.\n");
    fflush(stdout);
    PIN_StartProgram();

    return 0;
}

