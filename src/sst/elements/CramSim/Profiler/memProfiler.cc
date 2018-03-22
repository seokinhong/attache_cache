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
#include <algorithm>

#define INST_UNKNOWN 0
#define INST_SP_FP   1
#define INST_DP_FP   2
#define INST_INT     4


//This must be defined before inclusion of intttypes.h
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif


#undef __STDC_FORMAT_MACROS


KNOB<UINT64> MaxInstructions(KNOB_MODE_WRITEONCE, "pintool",
    "i", "100000", "Maximum number of instructions to run");
KNOB<UINT64> WarmupInstructions(KNOB_MODE_WRITEONCE, "pintool",
     "w","10000", "The number of instructions to warmup");
//"w","10", "The number of instructions to warmup");
KNOB<UINT32> SSTVerbosity(KNOB_MODE_WRITEONCE, "pintool",
    "v", "0", "SST verbosity level");
KNOB<UINT32> MaxCoreCount(KNOB_MODE_WRITEONCE, "pintool",
    "c", "1", "Maximum core count to use for data pipes.");
KNOB<UINT32> SimilarityDistance(KNOB_MODE_WRITEONCE, "pintool",
    "a", "0", "Default SST Memory Pool");
KNOB<UINT32> RecordRandomness(KNOB_MODE_WRITEONCE, "pintool", "r", "0", "Record Randomness (0 = diabled, 1 = enabled");
KNOB<UINT32> Randomness_window(KNOB_MODE_WRITEONCE, "pintool", "rw", "0", "the number of memory access in a randomness window");
KNOB<UINT32> MemCompProfile(KNOB_MODE_WRITEONCE, "pintool",
    "x", "0", "Enable memory compression profiler");
KNOB<string> OutputFileName(KNOB_MODE_WRITEONCE, "pintool",
    "f", "memprofile", "output file name");
KNOB<string> MemCompTraceInterval(KNOB_MODE_WRITEONCE, "pintool",
    "n", "1000000", "Memory compression profile interval");
KNOB<UINT32> CacheSets(KNOB_MODE_WRITEONCE, "pintool",
    "cs", "1024", "the number of cache sets");
KNOB<UINT32> CacheWays(KNOB_MODE_WRITEONCE, "pintool",
    "cw", "8", "the number of cache way");
KNOB<UINT32> CacheReplPolicy(KNOB_MODE_WRITEONCE, "pintool",
    "cp", "0", "cache replacement policy");

#define MY_MAX(a,b) \
   ({ __typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })

PIN_LOCK mainLock;
PIN_LOCK mallocIndexLock;

UINT64 inst_cnt =0;
UINT64 max_insts=0;

UINT32 funcProfileLevel;
UINT32 core_count;
UINT64 warmup_insts=0;

bool enable_output;
bool content_copy_en=false;
bool randomness_profile_en=false;
UINT64 randomness_window=0;
UINT64 mem_access_counter=0;
UINT64 page_mask  = ~(4*1024-1);
std::vector<UINT64> page_history_table;
std::map<UINT64,UINT64> page_access_count;
UINT64 accumulated_accessed_page=0;
UINT64 randomness_window_cnt=0;
int cache_sets;
int cache_ways;
int cache_repl_policy;

UINT64 g_similarity_comp_cnt=0;
UINT64 g_similarity_incomp_cnt=0;
UINT64 g_accessed_cacheline_num=0;
int similarity_distance=0;
FILE * outputFile;


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


int64_t getSignedExtension(int64_t data, uint32_t size) {
	assert(size<=65);
	int64_t new_data;
	new_data=(data<<(64-size))>>(64-size);
	return new_data;
}




////// Page Allocator ////////
UINT32 memSize;
UINT32 pageSize;
std::map<UINT64,UINT64> allocatedPage;
std::map<UINT64,UINT64> pageTable;


UINT64 getPhyAddress(UINT64 virtAddr)
{
	//get physical address
	const uint64_t pageOffset = virtAddr % pageSize;
	const uint64_t virtPageStart = virtAddr - pageOffset;
	UINT64 l_nextPageAddress;

	std::map<uint64_t, uint64_t>::iterator findEntry = pageTable.find(virtPageStart);

	if(findEntry!=pageTable.end())
	{
		l_nextPageAddress = findEntry->second;
	}
	else
	{
		UINT64 nextAddress_tmp = rand() % memSize;
		UINT64 offset = nextAddress_tmp % pageSize;
		UINT64 l_nextPageAddress = nextAddress_tmp - offset;

		while (allocatedPage.end() != allocatedPage.find(l_nextPageAddress)) {
			uint64_t nextAddress_tmp = rand() % memSize;
			uint64_t offset = nextAddress_tmp % pageSize;
			l_nextPageAddress = nextAddress_tmp - offset;
		}

		printf("m_nextPageAddress:$%llx\n", l_nextPageAddress);
		allocatedPage[l_nextPageAddress] = 1;
		pageTable[virtPageStart]=l_nextPageAddress;


		if (l_nextPageAddress + pageSize > memSize) {
			fprintf(stderr, "[memController] Out of Address Range!!, nextPageAddress:%lld pageSize:%lld memsize:%lld\n",
					l_nextPageAddress, pageSize, memSize);
			fflush(stderr);
			exit(-1);
		}
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

Flag mcache_access(MCache *c, Addr addr, Flag dirty)
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
			return HIT;
		}
	}

	//even on a miss, we need to know which set was accessed
	c->touched_wayid = 0;
	c->touched_setid = set;
	c->touched_lineid = start;

	c->s_miss++;
	return MISS;
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




VOID CacheAccess(UINT64 ADDR)
{
	UINT64 paddr = getPhyAddress(ADDR);
	bool isHit=mcache_access(cache,paddr,0);
	mcache_install(cache,paddr,0);
}

///// Profilers //////
VOID RandomnessRecord(UINT64 addr)
{
	UINT64 page_num=addr&page_mask;
	std::vector<UINT64>::iterator iter = std::find(page_history_table.begin(), page_history_table.end(), page_num);
	if (iter == page_history_table.end())
		page_history_table.push_back(page_num);


	mem_access_counter++;
	if(mem_access_counter>=randomness_window)
	{
		UINT64 accessed_page=page_history_table.size();
		accumulated_accessed_page+=accessed_page;
		randomness_window_cnt++;
		fprintf(outputFile,"%lld\t%lld\n",randomness_window_cnt,accessed_page);
		mem_access_counter=0;
		page_history_table.clear();
	}
	page_access_count[page_num]++;
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
		fprintf(stderr,"similarity:%f", (g_similarity_incomp_cnt+g_similarity_comp_cnt)/g_accessed_cacheline_num);

	}

	std::cout<<"Average page access count:"<<(UINT64)((double)accumulated_accessed_page/(double)randomness_window_cnt)<<std::endl;
	std::cout<<"cache access:"<<cache->s_count<<std::endl;
	std::cout<<"cache miss:"<<cache->s_miss<<std::endl;
	std::cout<<"allocated pages:"<<pageTable.size();

	UINT64 pagenum=0;
	for(auto&counter : page_access_count)
	{
		fprintf(outputFile,"page%lld:%lld\n",pagenum++,counter.second);
	}
	fclose(outputFile);
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

	if(inst_cnt>(warmup_insts+max_insts))
		PIN_ExitApplication(1);



        if(thread_instr_id[thr].ip!=ip)
        {
            fprintf(stderr,"ip is mismatch\n");
            exit(-1);
        }

		if(content_copy_en) {
			//assume that cache line size is 64B
			uint64_t *data = (uint64_t *) malloc(sizeof(uint64_t) * 8);
			ReadCacheLine(addr, data);
//#ifdef COMP_DEBUG
//            fprintf(stderr,"[PINTOOL] [%d] write addr:%llx data:%llx ac.inst.data:%llx\n", i, addr, *(data+i), ac.inst.data[i]);
//#endif


			//checkSimilarity(data, addr);
			free(data);
		}
		RandomnessRecord(addr);
		CacheAccess(addr);

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

	if(inst_cnt>(warmup_insts+max_insts))
		PIN_ExitApplication(1);


	if(thread_instr_id[thr].ip!=ip)
	{
		fprintf(stderr,"ip is mismatch\n");
		exit(-1);
	}


	if(thread_instr_id[thr].ip!=ip){
		fprintf(stderr,"ip is mismatch\n");
		exit(-1);
	}


	if(content_copy_en) {
		//assume that cache line size is 64B
		uint64_t *data = (uint64_t *) malloc(sizeof(uint64_t) * 8);
		ReadCacheLine(addr, data);
//#ifdef COMP_DEBUG
//            fprintf(stderr,"[PINTOOL] [%d] write addr:%llx data:%llx ac.inst.data:%llx\n", i, addr, *(data+i), ac.inst.data[i]);
//#endif

		//checkSimilarity(data, addr);
		free(data);
	}

	RandomnessRecord(addr);
	CacheAccess(addr);

}


VOID WriteInstructionReadWrite(THREADID thr, ADDRINT ip, UINT32 instClass,
	UINT32 simdOpWidth ) {

        const uint64_t readAddr=(uint64_t) thread_instr_id[thr].raddr;
        const uint32_t readSize=(uint32_t) thread_instr_id[thr].rsize;
        const uint64_t writeAddr=(uint64_t) thread_instr_id[thr].waddr;
        const uint32_t writeSize=(uint32_t) thread_instr_id[thr].wsize;
	if(enable_output) {
		if(thr < core_count) {
			WriteInstructionRead(  readAddr,  readSize,  thr, ip, instClass, simdOpWidth );
			WriteInstructionWrite( writeAddr, writeSize, thr, ip, instClass, simdOpWidth );
		}
	}
}

VOID WriteInstructionReadOnly(THREADID thr, ADDRINT ip,
	UINT32 instClass, UINT32 simdOpWidth) {
        const uint64_t readAddr=(uint64_t) thread_instr_id[thr].raddr;
        const uint32_t readSize=(uint32_t) thread_instr_id[thr].rsize;
       
        if(enable_output) {
		if(thr < core_count) {
			WriteInstructionRead(  readAddr,  readSize,  thr, ip, instClass, simdOpWidth );
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

}

VOID WriteInstructionWriteOnly(THREADID thr, ADDRINT ip,
	UINT32 instClass, UINT32 simdOpWidth) {
         const uint64_t writeAddr=(uint64_t) thread_instr_id[thr].waddr;
        const uint32_t writeSize=(uint32_t) thread_instr_id[thr].wsize;
       

	if(enable_output) {
		if(thr < core_count) {
                        WriteInstructionWrite(writeAddr, writeSize,  thr, ip, instClass, simdOpWidth);
		}
	}

}


VOID InstrumentInstruction(INS ins, VOID *v)
{
	UINT32 simdOpWidth     = 1;
	UINT32 instClass       = INST_UNKNOWN;
	UINT32 maxSIMDRegWidth = 1;


		std::string instCode = INS_Mnemonic(ins);

		for (UINT32 i = 0; i < INS_MaxNumRRegs(ins); i++) {
			if (REG_is_xmm(INS_RegR(ins, i))) {
				maxSIMDRegWidth = MY_MAX(maxSIMDRegWidth, 2);
			} else if (REG_is_ymm(INS_RegR(ins, i))) {
				maxSIMDRegWidth = MY_MAX(maxSIMDRegWidth, 4);
			} else if (REG_is_zmm(INS_RegR(ins, i))) {
				maxSIMDRegWidth = MY_MAX(maxSIMDRegWidth, 8);
			}
		}

		for (UINT32 i = 0; i < INS_MaxNumWRegs(ins); i++) {
			if (REG_is_xmm(INS_RegW(ins, i))) {
				maxSIMDRegWidth = MY_MAX(maxSIMDRegWidth, 2);
			} else if (REG_is_ymm(INS_RegW(ins, i))) {
				maxSIMDRegWidth = MY_MAX(maxSIMDRegWidth, 4);
			} else if (REG_is_zmm(INS_RegW(ins, i))) {
				maxSIMDRegWidth = MY_MAX(maxSIMDRegWidth, 8);
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
					instClass = INST_DP_FP;
				} else if ((suffix == "PS") || (suffix == "ps")) {
					simdOpWidth = maxSIMDRegWidth * 2;
					instClass = INST_SP_FP;
				} else if ((suffix == "SD") || (suffix == "sd")) {
					simdOpWidth = 1;
					instClass = INST_DP_FP;
				} else if ((suffix == "SS") || (suffix == "ss")) {
					simdOpWidth = 1;
					instClass = INST_SP_FP;
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

}

VOID InstrumentRoutine(RTN rtn, VOID* args) {
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
            std::cout << " max instruction count: " <<
            MaxInstructions.Value() << " warmup instruction count: " <<
            WarmupInstructions.Value() <<
            " max core count: " << MaxCoreCount.Value() << std::endl;
    }

    core_count = MaxCoreCount.Value();
	std::string output_filename=OutputFileName.Value();
	outputFile=fopen(output_filename.c_str(),"w");

    fflush(stdout);

    sleep(1);

	max_insts= MaxInstructions.Value();
    warmup_insts = WarmupInstructions.Value();
	similarity_distance = SimilarityDistance.Value();
	randomness_profile_en = RecordRandomness.Value();
	randomness_window=Randomness_window.Value();
	if(randomness_profile_en==1 && randomness_window==0)
	{
		fprintf(stderr, "RANDOMNESS WINDOW ERROR");
		exit(-1);
	}


	std::cout<<"warmup_insts: "<<warmup_insts<<std::endl;
	std::cout<<"max_insts: "<<max_insts<<std::endl;
	std::cout<<"randomness_profile_en: "<<randomness_profile_en<<std::endl;
	std::cout<<"randomness_window: "<<randomness_window<<std::endl;

    INS_AddInstrumentFunction(InstrumentInstruction, 0);
    RTN_AddInstrumentFunction(InstrumentRoutine, 0);

	cache_sets=CacheSets.Value();
	cache_ways=CacheWays.Value();
	cache_repl_policy=CacheReplPolicy.Value();
	cache=(MCache*) mcache_new(cache_sets,cache_ways,cache_repl_policy);


    fflush(stdout);
    PIN_StartProgram();

    return 0;
}

