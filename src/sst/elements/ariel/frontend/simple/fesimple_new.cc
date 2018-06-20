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
#include <vector>
#include <math.h>
#include <algorithm>



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
KNOB<string> MemProfileName(KNOB_MODE_WRITEONCE, "pintool",
    "f", "none", "Memory compression profile trace file name");
KNOB<UINT32> ProfilingMode(KNOB_MODE_WRITEONCE, "pintool",
    "o", "0", "pintool mode, 0=trace gen for sst, 1=standalone");
KNOB<UINT32> CacheSets(KNOB_MODE_WRITEONCE, "pintool",
					   "cs", "1024", "the number of cache sets");
KNOB<UINT32> CacheWays(KNOB_MODE_WRITEONCE, "pintool",
					   "cw", "16", "the number of cache way");
KNOB<UINT32> CacheReplPolicy(KNOB_MODE_WRITEONCE, "pintool",
							 "cp", "0", "cache replacement policy");



#define ARIEL_MAX(a,b) \
   ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })

typedef struct {
	int64_t insExecuted;
} ArielFunctionRecord;

UINT64 inst_cnt =0;
UINT64 max_insts = 0;
UINT64 warmup_insts=0;

UINT32 funcProfileLevel;
UINT32 core_count;
UINT32 default_pool;


UINT64 cnt_cache_access_trace=0;
UINT64 cnt_cache_miss_trace=0;
UINT64 cnt_allocated_page_trace=0;

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
UINT64 recordInterval=100000000;
bool profilingMode=true;
int cache_sets=0;
int cache_ways=0;
int cache_repl_policy=0;
int dramPageSize=8*1024;
int page_offset=log2(dramPageSize);
std::map<UINT64,UINT64> accessed_page;
FILE * outputFile;
bool profile_file_out_en=false;

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
UINT64 cnt_cl_size25=0;
UINT64 cnt_cl_size50=0;
UINT64 cnt_cl_size75=0;
UINT64 cnt_cl_size100=0;
UINT64 cnt_page_all16=0;
UINT64 cnt_page_all32=0;
UINT64 cnt_page_all48=0;
UINT64 cnt_page_all64=0;
bool doVerify=false;

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
	uint64_t addr_new = (addr>>6)<<6;
	int copied_size= PIN_SafeCopy((uint8_t*)data, (VOID*) addr_new, 64);
	if(copied_size !=64)
		fprintf(stderr, "ariel memory copy fail\n");
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


////// Page Allocator ////////
UINT64 memSize = 34359738368; //32GB
UINT32 pageSize = 4*1024; //
UINT64 pageCount = 0;
std::map<UINT64,UINT64> allocatedPage;
std::map<UINT64,UINT64> pageTable;

std::mt19937_64 rng;
void seed(uint64_t new_seed = std::mt19937_64::default_seed) {
	rng.seed(new_seed);
}

uint64_t randGen() {
	return rng(); }


UINT64 getPhyAddress(UINT64 virtAddr)
{
	//get physical address
	const uint64_t pageOffset = virtAddr % pageSize;
	const uint64_t virtPageStart = virtAddr - pageOffset;
	int retry_cnt=0;
	UINT64 l_nextPageAddress;

	std::map<uint64_t, uint64_t>::iterator findEntry = pageTable.find(virtPageStart);

	if (findEntry != pageTable.end()) {
		l_nextPageAddress = findEntry->second;
	}
	else {
		uint64_t rand_num = randGen();
		uint64_t nextAddress_tmp = rand_num % memSize;
		uint64_t offset = nextAddress_tmp % pageSize;
		l_nextPageAddress = nextAddress_tmp - offset;

		retry_cnt = 0;
		while (allocatedPage.end() != allocatedPage.find(l_nextPageAddress)) {
			uint64_t nextAddress_tmp = randGen() % memSize;
			uint64_t offset = nextAddress_tmp % pageSize;
			l_nextPageAddress = nextAddress_tmp - offset;
			if (++retry_cnt > 10000000) {
				printf("allocated page:%lld, clear the allocated page table\n", pageCount);
				allocatedPage.clear();
			}
			//       printf("same physical address is detected, new addr:%llx\n",l_nextPageAddress);
		}

		allocatedPage[l_nextPageAddress] = 1;
		pageTable[virtPageStart] = l_nextPageAddress;

		if (l_nextPageAddress + pageSize > memSize) {
			fprintf(stderr, "[memController] Out of Address Range!!, nextPageAddress:%lld pageSize:%lld memsize:%lld\n",
					l_nextPageAddress, pageSize, memSize);
			fflush(stderr);
			exit(-1);
		}

		pageCount++;

		if(enable_output)
			cnt_allocated_page_trace++;
	}
	return l_nextPageAddress;
}


//// Cache //////


#define MCACHE_SRRIP_MAX  7
#define MCACHE_SRRIP_INIT 1
#define MCACHE_PSEL_MAX    1023
#define MCACHE_LEADER_SETS  32

#define FALSE 0
#define TRUE  1

#define HIT   1
#define MISS  0


#define CLOCK_INC_FACTOR 4

#define MAX_UNS 0xffffffff

#define ASSERTM(cond, msg...) if(!(cond) ){ printf(msg); fflush(stdout);} assert(cond);
#define DBGMSG(cond, msg...) if(cond){ printf(msg); fflush(stdout);}

#define SAT_INC(x,max)   (x<max)? x+1:x
#define SAT_DEC(x)       (x>0)? x-1:0



typedef unsigned	    uns;
typedef unsigned char	    uns8;
typedef unsigned short	    uns16;
typedef unsigned	    uns32;
typedef unsigned long long  uns64;
typedef short		    int16;
typedef int		    int32;
typedef int long long	    int64;
typedef int		    Generic_Enum;


/* Conventions */
typedef uns64		    Addr;
typedef uns32		    Binary;
typedef uns8		    Flag;

typedef uns64               Counter;
typedef int64               SCounter;


typedef struct MCache_Entry {
    Flag    valid;
    Flag    dirty;
    Addr    tag;
    uns     ripctr;
    uns64   last_access;
}MCache_Entry;



typedef enum MCache_ReplPolicy_Enum {
    REPL_LRU=0,
    REPL_RND=1,
    REPL_SRRIP=2,
    REPL_DRRIP=3,
    REPL_FIFO=4,
    REPL_DIP=5,
    NUM_REPL_POLICY=6
} MCache_ReplPolicy;


typedef struct MCache{
    uns sets;
    uns assocs;
    MCache_ReplPolicy repl_policy; //0:LRU  1:RND 2:SRRIP
    uns index_policy; // how to index cache

    Flag *is_leader_p0; // leader SET for D(RR)IP
    Flag *is_leader_p1; // leader SET for D(RR)IP
    uns psel;

    MCache_Entry *entries;
    uns *fifo_ptr; // for fifo replacement (per set)
    int touched_wayid;
    int touched_setid;
    int touched_lineid;

    uns64 s_count; // number of accesses
    uns64 s_miss; // number of misses
    uns64 s_evict; // number of evictions
} MCache;

MCache* cache;


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns mcache_get_index(MCache *c, Addr addr){
	uns retval;

	switch(c->index_policy){
		case 0:
			retval=addr%c->sets;
			break;

		default:
			exit(-1);
	}

	return retval;
}


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

Flag    mcache_mark_dirty    (MCache *c, Addr addr)
{
	Addr  tag  = addr; // full tags
	uns   set  = mcache_get_index(c,addr);
	uns   start = set * c->assocs;
	uns   end   = start + c->assocs;
	uns   ii;

	for (ii=start; ii<end; ii++){
		MCache_Entry *entry = &c->entries[ii];
		if(entry->valid && (entry->tag == tag))
		{
			entry->dirty = TRUE;
			return TRUE;
		}
	}

	return FALSE;
}
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

void mcache_select_leader_sets(MCache *c, uns sets){
	uns done=0;

	c->is_leader_p0  = (Flag *) calloc (sets, sizeof(Flag));
	c->is_leader_p1  = (Flag *) calloc (sets, sizeof(Flag));

	while(done <= MCACHE_LEADER_SETS){
		uns randval=rand()%sets;
		if( (c->is_leader_p0[randval]==FALSE)&&(c->is_leader_p1[randval]==FALSE)){
			c->is_leader_p0[randval]=TRUE;
			done++;
		}
	}

	done=0;
	while(done <= MCACHE_LEADER_SETS){
		uns randval=rand()%sets;
		if( (c->is_leader_p0[randval]==FALSE)&&(c->is_leader_p1[randval]==FALSE)){
			c->is_leader_p1[randval]=TRUE;
			done++;
		}
	}
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
Flag mcache_dip_check_lru_update(MCache *c, uns set){
	Flag update_lru=TRUE;

	if(c->is_leader_p0[set]){
		if(c->psel<MCACHE_PSEL_MAX){
			c->psel++;
		}
		update_lru=FALSE;
		if(rand()%100<5) update_lru=TRUE; // BIP
	}

	if(c->is_leader_p1[set]){
		if(c->psel){
			c->psel--;
		}
		update_lru=1;
	}

	if( (c->is_leader_p0[set]==FALSE)&& (c->is_leader_p1[set]==FALSE)){
		if(c->psel >= (MCACHE_PSEL_MAX+1)/2){
			update_lru=1; // policy 1 wins
		}else{
			update_lru=FALSE; // policy 0 wins
			if(rand()%100<5) update_lru=TRUE; // BIP
		}
	}

	return update_lru;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
uns mcache_drrip_get_ripctrval(MCache *c, uns set){
	uns ripctr_val=MCACHE_SRRIP_INIT;

	if(c->is_leader_p0[set]){
		if(c->psel<MCACHE_PSEL_MAX){
			c->psel++;
		}
		ripctr_val=0;
		if(rand()%100<5) ripctr_val=1; // BIP
	}

	if(c->is_leader_p1[set]){
		if(c->psel){
			c->psel--;
		}
		ripctr_val=1;
	}

	if( (c->is_leader_p0[set]==FALSE)&& (c->is_leader_p1[set]==FALSE)){
		if(c->psel >= (MCACHE_PSEL_MAX+1)/2){
			ripctr_val=1; // policy 1 wins
		}else{
			ripctr_val=0; // policy 0 wins
			if(rand()%100<5) ripctr_val=1; // BIP
		}
	}


	return ripctr_val;
}



////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns mcache_find_victim_lru (MCache *c,  uns set)
{
	uns start = set   * c->assocs;
	uns end   = start + c->assocs;
	uns lowest=start;
	uns ii;


	for (ii = start; ii < end; ii++){
		if (c->entries[ii].last_access < c->entries[lowest].last_access){
			lowest = ii;
		}
	}

	return lowest;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns mcache_find_victim_rnd (MCache *c,  uns set)
{
	uns start = set   * c->assocs;
	uns victim = start + rand()%c->assocs;

	return  victim;
}



////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns mcache_find_victim_srrip (MCache *c,  uns set)
{
	uns start = set   * c->assocs;
	uns end   = start + c->assocs;
	uns ii;
	uns victim = end; // init to impossible

	while(victim == end){
		for (ii = start; ii < end; ii++){
			if (c->entries[ii].ripctr == 0){
				victim = ii;
				break;
			}
		}

		if(victim == end){
			for (ii = start; ii < end; ii++){
				c->entries[ii].ripctr--;
			}
		}
	}

	return  victim;
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns mcache_find_victim_fifo (MCache *c,  uns set)
{
	uns start = set   * c->assocs;
	uns retval = start + c->fifo_ptr[set];
	return retval;
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uns mcache_find_victim (MCache *c, uns set)
{
	int ii;
	int start = set   * c->assocs;
	int end   = start + c->assocs;

	//search for invalid first
	for (ii = start; ii < end; ii++){
		if(!c->entries[ii].valid){
			return ii;
		}
	}


	switch(c->repl_policy){
		case REPL_LRU:
			return mcache_find_victim_lru(c, set);
		case REPL_RND:
			return mcache_find_victim_rnd(c, set);
		case REPL_SRRIP:
			return mcache_find_victim_srrip(c, set);
		case REPL_DRRIP:
			return mcache_find_victim_srrip(c, set);
		case REPL_FIFO:
			return mcache_find_victim_fifo(c, set);
		case REPL_DIP:
			return mcache_find_victim_lru(c, set);
		default:
			assert(0);
	}

	return -1;

}



////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

MCache *mcache_new(uns sets, uns assocs, uns repl_policy )
{
	MCache *c = (MCache *) calloc (1, sizeof (MCache));
	c->sets    = sets;
	c->assocs  = assocs;
	c->repl_policy = (MCache_ReplPolicy)repl_policy;

	c->entries  = (MCache_Entry *) calloc (sets * assocs, sizeof(MCache_Entry));


	c->fifo_ptr  = (uns *) calloc (sets, sizeof(uns));

	//for drrip or dip
	mcache_select_leader_sets(c,sets);
	c->psel=(MCACHE_PSEL_MAX+1)/2;


	return c;
}

bool mcache_access(MCache *c, Addr addr, Flag dirty)
{
	Addr  tag  = addr; // full tags
	uns   set  = mcache_get_index(c,addr);
	uns   start = set * c->assocs;
	uns   end   = start + c->assocs;
	uns   ii;

	c->s_count++;

	for (ii=start; ii<end; ii++){
		MCache_Entry *entry = &c->entries[ii];

		if(entry->valid && (entry->tag == tag))
		{
			entry->last_access  = c->s_count;
			entry->ripctr       = MCACHE_SRRIP_MAX;
			c->touched_wayid = (ii-start);
			c->touched_setid = set;
			c->touched_lineid = ii;
			if(dirty==TRUE) //If the operation is a WB then mark it as dirty
			{
				mcache_mark_dirty(c,tag);
			}
			//printf("find %llx at %d\n",addr, ii);
			return true;
		}
	}

	//even on a miss, we need to know which set was accessed
	c->touched_wayid = 0;
	c->touched_setid = set;
	c->touched_lineid = start;

	c->s_miss++;
	return false;
}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

Flag    mcache_probe    (MCache *c, Addr addr)
{
	Addr  tag  = addr; // full tags
	uns   set  = mcache_get_index(c,addr);
	uns   start = set * c->assocs;
	uns   end   = start + c->assocs;
	uns   ii;

	for (ii=start; ii<end; ii++){
		MCache_Entry *entry = &c->entries[ii];
		if(entry->valid && (entry->tag == tag))
		{
			return TRUE;
		}
	}

	return FALSE;
}


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

Flag    mcache_invalidate    (MCache *c, Addr addr)
{
	Addr  tag  = addr; // full tags
	uns   set  = mcache_get_index(c,addr);
	uns   start = set * c->assocs;
	uns   end   = start + c->assocs;
	uns   ii;

	for (ii=start; ii<end; ii++){
		MCache_Entry *entry = &c->entries[ii];
		if(entry->valid && (entry->tag == tag))
		{
			entry->valid = FALSE;
			return TRUE;
		}
	}

	return FALSE;
}


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

void    mcache_swap_lines(MCache *c, uns set, uns way_ii, uns way_jj)
{
	uns   start = set * c->assocs;
	uns   loc_ii   = start + way_ii;
	uns   loc_jj   = start + way_jj;

	MCache_Entry tmp = c->entries[loc_ii];
	c->entries[loc_ii] = c->entries[loc_jj];
	c->entries[loc_jj] = tmp;

}



////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

MCache_Entry mcache_install(MCache *c, Addr addr, Flag dirty)
{
	Addr  tag  = addr; // full tags
	uns   set  = mcache_get_index(c,addr);
	uns   start = set * c->assocs;
	uns   end   = start + c->assocs;
	uns   ii, victim;

	Flag update_lrubits=TRUE;

	MCache_Entry *entry;
	MCache_Entry evicted_entry;

	for (ii=start; ii<end; ii++){
		entry = &c->entries[ii];
		if(entry->valid && (entry->tag == tag)){
			printf("Installed entry already with addr:%llx present in set:%u\n", addr, set);
			fflush(stdout);
			sleep(1);
			exit(-1);
		}
	}

	// find victim and install entry

	victim = mcache_find_victim(c, set);
	entry = &c->entries[victim];
	evicted_entry =c->entries[victim];
	if(entry->valid){
		c->s_evict++;
	}

	//udpate DRRIP info and select value of ripctr
	uns ripctr_val=MCACHE_SRRIP_INIT;

	if(c->repl_policy==REPL_DRRIP){
		ripctr_val=mcache_drrip_get_ripctrval(c,set);
	}

	if(c->repl_policy==REPL_DIP){
		update_lrubits=mcache_dip_check_lru_update(c,set);
	}


	//put new information in
	entry->tag   = tag;
	entry->valid = TRUE;
	if(dirty==TRUE)
		entry->dirty=TRUE;
	else
		entry->dirty = FALSE;
	entry->ripctr  = ripctr_val;

	if(update_lrubits){
		entry->last_access  = c->s_count;
	}



	c->fifo_ptr[set] = (c->fifo_ptr[set]+1)%c->assocs; // fifo update

	c->touched_lineid=victim;
	c->touched_setid=set;
	c->touched_wayid=victim-(set*c->assocs);

	return evicted_entry;
}




UINT64 cache_miss_cnt=0;
UINT64 cache_access_cnt=0;
bool isHit=false;
VOID CacheAccess(UINT64 ADDR)
{
	UINT64 paddr = getPhyAddress(ADDR);
	UINT64 cache_addr = paddr >> (int)page_offset;

	isHit=mcache_access(cache,cache_addr,0);

	if(!isHit) {
		mcache_install(cache, cache_addr, 0);
		cache_miss_cnt++;
	}
	cache_access_cnt++;

	if(accessed_page.find(paddr)==accessed_page.end())
		accessed_page[paddr]=0;
	else
		accessed_page[paddr]++;

	if(enable_output){
		cnt_cache_access_trace++;
		if(!isHit)
			cnt_cache_miss_trace++;
	}
	//printf("addr:%llx paddr:%llx tag:%llx isHit:%d\n",ADDR,paddr,tag,isHit==true?1:0);
	//fflush(stdout);
}


VOID RecordStatistics() {
	float miss_rate = (float) ((double) cache_miss_cnt / (double) cache_access_cnt);
/*	float all_size16_rate = (float) ((double) cnt_page_all16 / (double) cache_access_cnt);
	float all_size32_rate = (float) ((double) cnt_page_all32 / (double) cache_access_cnt);
	float all_size48_rate = (float) ((double) cnt_page_all48 / (double) cache_access_cnt);

	float cl_size16_rate = (float) ((double) cnt_cl_size16 / (double) cache_access_cnt);
	float cl_size32_rate = (float) ((double) cnt_cl_size32 / (double) cache_access_cnt);
	float cl_size48_rate = (float) ((double) cnt_cl_size48 / (double) cache_access_cnt);
	fprintf(outputFile, "%.4f,%lld,%lld,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", miss_rate, cache_access_cnt, pageCount, cl_size16_rate,
			cl_size32_rate, cl_size48_rate, all_size16_rate, all_size32_rate, all_size48_rate);*/
//	fprintf(outputFile,"%f,%lld,%lld\n", miss_rate, cache_access_cnt,pageCount);
//	printf("%lld,%.4f,%lld,%lld,%lld\n", inst_cnt,miss_rate,cache_miss_cnt,cache_access_cnt,pageCount);
	fprintf(outputFile,"%lld,%.4f,%lld,%lld,%lld,%lld\n", inst_cnt,miss_rate,cache_miss_cnt,cache_access_cnt,pageCount,accessed_page.size());

//	fflush(stdout);
	fflush(outputFile);
	cache_miss_cnt = 0;
	cache_access_cnt = 0;
	accessed_page.clear();
/*	cnt_page_all16 = 0;
	cnt_page_all32 = 0;
	cnt_page_all48 = 0;
	cnt_cl_size16 = 0;
	cnt_cl_size32 = 0;
	cnt_cl_size48 = 0;*/
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

/****************************************************************/
/******************** END SHADOW STACK **************************/
/****************************************************************/
typedef struct page_compinfo{
	std::map<uint8_t, uint64_t> column_comp_rate;
	std::map<uint8_t, uint64_t> column_access;
	uint64_t cnt_cl_size25_access;
	uint64_t cnt_cl_size50_access;
	uint64_t cnt_cl_size75_access;
	uint64_t cnt_access;
}PAGE_COMPINFO;

std::map<uint64_t,PAGE_COMPINFO> page_comp_map;

VOID recordCompression(UINT64 word_addr)
{
	//get physical address
	uint64_t cl_addr=(word_addr>>6)<<6;
	const uint64_t pageOffset = cl_addr % pageSize;
	const uint64_t virtPageStart = cl_addr - pageOffset;
	int cl_size = 64;
	int num_cl_in_page = pageSize/cl_size;
	int cmp_size=0;
	int tmp_cnt_cl_size25=0;
	int tmp_cnt_cl_size50=0;
	int tmp_cnt_cl_size75=0;
	uint8_t column = pageOffset>>6;

	//todo
	uint64_t *data = (uint64_t *) malloc(sizeof(uint64_t) * 8);
	ReadCacheLine((uint64_t)cl_addr,data);
	uint64_t comp_size=getCompressedSize((uint8_t*)data);
	uint8_t cl_comp_ratio=((double)comp_size/(double)512)*100;


	//allocate new entry in the page compression info map.
	if(page_comp_map.find(virtPageStart)==page_comp_map.end())
	{
		PAGE_COMPINFO page_comp_info;
		page_comp_info.cnt_cl_size25_access=0;
		page_comp_info.cnt_cl_size50_access=0;
		page_comp_info.cnt_cl_size75_access=0;
		page_comp_info.cnt_access=0;
		page_comp_map[virtPageStart]=page_comp_info;
	}

	//allocate new column in an entry of the page compression info map.
	PAGE_COMPINFO &page_comp_info = page_comp_map[virtPageStart];
	if(page_comp_info.column_comp_rate.find(column)==page_comp_info.column_comp_rate.end()) {
		page_comp_info.column_comp_rate[column] = cl_comp_ratio;
		page_comp_info.column_access[column] = 1;
	}
	else
	{
		page_comp_info.column_comp_rate[column] += cl_comp_ratio;
		page_comp_info.column_access[column]++;
	}


	if(cl_comp_ratio<=25) {
		cnt_cl_size25++;
		page_comp_info.cnt_cl_size25_access++;
	}

	if(cl_comp_ratio<=50) {
		cnt_cl_size50++;
		page_comp_info.cnt_cl_size50_access++;
	}

	if(cl_comp_ratio<=75) {
		cnt_cl_size75++;
		page_comp_info.cnt_cl_size75_access++;
	}
	page_comp_info.cnt_access++;

/*	printf("------------------------\n");
	printf("page start address:%lld\n",virtPageStart);
	printf("addr: %llx\n",cl_addr);
	printf("pageSize: %d\n",pageSize);
	printf("pageoffset: %llx\n",pageOffset);
	printf("column: %d\n",column);
	printf("comp size: %d\n",comp_size);
	printf("comp ratio: %d\n",cl_comp_ratio);
	printf("cnt_cl_size25_access:%lld\n",page_comp_map[virtPageStart].cnt_cl_size25_access);
	printf("cnt_cl_size50_access:%lld\n",page_comp_map[virtPageStart].cnt_cl_size50_access);
	printf("cnt_cl_size75_access:%lld\n",page_comp_map[virtPageStart].cnt_cl_size75_access);
	printf("cnt_access:%lld\n",page_comp_map[virtPageStart].cnt_access);
	for(uint8_t i=0;i<cl_size;i++)
	{
		std::map<uint8_t, uint64_t> & column_comp_rate = page_comp_map[virtPageStart].column_comp_rate;
		std::map<uint8_t, uint64_t> & column_access = page_comp_map[virtPageStart].column_access;
		if(column_comp_rate.find(i)!=column_comp_rate.end())
		{
			uint64_t accumulated_comp_rate = column_comp_rate[i];
			uint64_t avg_comp_rate = (uint64_t)((double )accumulated_comp_rate/(double)column_access[i]);
			printf("column id[%d] avg_com_rate: %d\n",i,avg_comp_rate);
		}
	}
	printf("\n");*/



	free(data);
}

VOID PrintFiniMessage()
{
	std::cout<<"cache_access:"<<cache->s_count<<std::endl;
	std::cout<<"cache_miss:"<<cache->s_miss<<std::endl;
	std::cout<<"cache_miss_rate:"<<(double)cache->s_miss/(double)cache->s_count<<std::endl;
	std::cout<<"allocated_pages:"<<pageTable.size()<<std::endl;

	std::cout<<"cacheline_comp_rate_25:"<<cnt_cl_size25<<std::endl;
	std::cout<<"cacheline_comp_rate_50:"<<cnt_cl_size50<<std::endl;
	std::cout<<"cacheline_comp_rate_75:"<<cnt_cl_size75<<std::endl;
	std::cout<<"page_comp_size_16B:"<<cnt_page_all16<<std::endl;
	std::cout<<"page_comp_size_32B:"<<cnt_page_all32<<std::endl;
	std::cout<<"page_comp_size_48B:"<<cnt_page_all48<<std::endl;
	std::cout<<"mem accesses:"<<cache_access_cnt<<std::endl;
    int pagenum=0;
    for(auto it:page_comp_map)
	{
		PAGE_COMPINFO &page_compinfo=it.second;
        printf("page_comp_info_page page:%d comp_rate_25:%lld comp_rate_50:%lld comp_rate_75:%lld access:%lld\n",
               pagenum,page_compinfo.cnt_cl_size25_access,page_compinfo.cnt_cl_size50_access,page_compinfo.cnt_cl_size75_access,page_compinfo.cnt_access);

        for(int j=0; j<pageSize/64;j++)
        {
            if(page_compinfo.column_comp_rate.find(j)!=page_compinfo.column_comp_rate.end())
            {
                uint8_t comprate = ((double)page_compinfo.column_comp_rate[j]/(double)page_compinfo.column_access[j]);
                printf("page_comp_info_column page:%d column:%d comp_rate:%d\n",pagenum,j,comprate);
            }
        }
		pagenum++;
	}
	fflush(stdout);
	fclose(outputFile);
}




VOID WriteInstructionRead(UINT64 addr, UINT32 size, THREADID thr, ADDRINT ip,
	UINT32 instClass, UINT32 simdOpWidth) {

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
		//assume that cache line size is 64B
		uint64_t *data = (uint64_t *) malloc(sizeof(uint64_t) * 8);
		ReadCacheLine(addr, data);
		int compressed_size=getCompressedSize((uint8_t*)data);
		uint8_t comp_ratio = (uint8_t)(((double)compressed_size/(double)512)*100);
		ac.inst.data[0]=comp_ratio;
		//ac.inst.data[0]=255;

		/*for (int i = 0; i < 8; i++) {
            ac.inst.data[i] = *(data + i);
        }*/
		free(data);
	}
	if(tunnel)
    	tunnel->writeMessage(thr, ac);
}

VOID WriteInstructionWrite(UINT64 addr, UINT32 size, THREADID thr, ADDRINT ip,
	UINT32 instClass, UINT32 simdOpWidth) {

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
		int compressed_size=getCompressedSize((uint8_t*)data);
		uint8_t comp_ratio = (uint8_t)(((double)compressed_size/(double)512)*100);
		ac.inst.data[0]=comp_ratio;
		//ac.inst.data[0]=255;

        /*for (int i = 0; i < 8; i++) {
            ac.inst.data[i] = *(data + i);
        }*/
        free(data);
    }

	if(tunnel)
    	tunnel->writeMessage(thr, ac);
}

VOID WriteStartInstructionMarker(UINT32 thr, ADDRINT ip) {
    	ArielCommand ac;
    	ac.command = ARIEL_START_INSTRUCTION;
    	ac.instPtr = (uint64_t) ip;

	if(tunnel)
		tunnel->writeMessage(thr, ac);
}

VOID WriteEndInstructionMarker(UINT32 thr, ADDRINT ip) {
    	ArielCommand ac;
    	ac.command = ARIEL_END_INSTRUCTION;
    	ac.instPtr = (uint64_t) ip;

	if(tunnel)
		tunnel->writeMessage(thr, ac);
}

VOID CycleTick()
{
 	inst_cnt++;
	if(profilingMode && inst_cnt%recordInterval==0) {
		RecordStatistics();
	}

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

	//for sst mode, print final message at this point
	if(inst_cnt==(warmup_insts+max_insts))
	{
		PrintFiniMessage();
		if(profilingMode==1)
		{
			PIN_ExitApplication(1);
		}

	}
}


VOID WriteInstructionReadWrite(THREADID thr, ADDRINT ip, UINT32 instClass,
	UINT32 simdOpWidth ) {

        const uint64_t readAddr=(uint64_t) thread_instr_id[thr].raddr;
        const uint32_t readSize=(uint32_t) thread_instr_id[thr].rsize;
        const uint64_t writeAddr=(uint64_t) thread_instr_id[thr].waddr;
        const uint32_t writeSize=(uint32_t) thread_instr_id[thr].wsize;

	CycleTick();
	if(profilingMode&&enable_output) {
		CacheAccess(readAddr);
		CacheAccess(writeAddr);
		recordCompression(readAddr);
		recordCompression(writeAddr);
	}
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

		CycleTick();
		if(profilingMode&&enable_output) {
			CacheAccess(readAddr);
			recordCompression(readAddr);
		}
        if(enable_output) {
            if(thr < core_count) {
                WriteStartInstructionMarker(thr, ip);
                WriteInstructionRead(  readAddr,  readSize,  thr, ip, instClass, simdOpWidth );
                WriteEndInstructionMarker(thr, ip);
            }
		}

}

VOID WriteNoOp(THREADID thr, ADDRINT ip) {
	CycleTick();
	if(enable_output) {
		if(thr < core_count) {
            		ArielCommand ac;
            		ac.command = ARIEL_NOOP;
            		ac.instPtr = (uint64_t) ip;

			if(tunnel)
				tunnel->writeMessage(thr, ac);
		}
	}

}

VOID WriteInstructionWriteOnly(THREADID thr, ADDRINT ip,
	UINT32 instClass, UINT32 simdOpWidth) {
         const uint64_t writeAddr=(uint64_t) thread_instr_id[thr].waddr;
        const uint32_t writeSize=(uint32_t) thread_instr_id[thr].wsize;
       
	CycleTick();
	if(profilingMode&&enable_output) {
		CacheAccess(writeAddr);
		recordCompression(writeAddr);
	}
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
	if(tunnel)
    	return tunnel->getCycles();
}

int mapped_gettimeofday(struct timeval *tp, void *tzp) {
    if ( tp == NULL ) { errno = EINVAL ; return -1; }


	if(tunnel)
		tunnel->getTime(tp);
    return 0;
}

void mapped_ariel_output_stats() {
    THREADID thr = PIN_ThreadId();
    ArielCommand ac;
    ac.command = ARIEL_OUTPUT_STATS;
    ac.instPtr = (uint64_t) 0;

	if(tunnel)
		tunnel->writeMessage(thr, ac);
}

// same effect as mapped_ariel_output_stats(), but it also sends a user-defined reference number back
void mapped_ariel_output_stats_buoy(uint64_t marker) {
    THREADID thr = PIN_ThreadId();
    ArielCommand ac;
    ac.command = ARIEL_OUTPUT_STATS;
    ac.instPtr = (uint64_t) marker; //user the instruction pointer slot to send the marker number

	if(tunnel)
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

	if(tunnel)
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


	if(tunnel)
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

	if(tunnel)
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

		if(tunnel)
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

		if(tunnel)
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

	if(tunnel)
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

	if(tunnel) {
		tunnel->writeMessage(0, ac);
		delete tunnel;
	}


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

	//PrintFiniMessage();

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

	//    enable_memcomp = MemCompProfile.value();
	//   memcomp_tracefile = MemCompTraceName();
	//  memcomp_interval = MemCompTraceInterval();
	warmup_insts = WarmupInstructions.Value();
	max_insts = MaxInstructions.Value();
	similarity_distance = SimilarityDistance.Value();

	profilingMode = ProfilingMode.Value();
	cache_sets=CacheSets.Value();
	cache_ways=CacheWays.Value();

	cache_repl_policy=CacheReplPolicy.Value();
	cache=(MCache*) mcache_new(cache_sets,cache_ways,cache_repl_policy);
	std::string profile_file=MemProfileName.Value();
	if(profile_file!="none") {
		outputFile = fopen(profile_file.c_str(), "w");
		profile_file_out_en=true;
	}
	std::cout<<"cache_sets: "<<cache_sets<<std::endl;
	std::cout<<"cache_ways: "<<cache_ways<<std::endl;
	std::cout<<"profiling mode: "<<profilingMode<<std::endl;
	std::cout<<"warmup_insts: "<<warmup_insts<<std::endl;

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


	if(profilingMode==false)
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


