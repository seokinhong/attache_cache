/**
 * @author Dimitrios Skarlantos
 * @version 0.1, 07/26/17
 * 
 * The Indigo Trace Tool is based on the SST trace tool
 * with optimized support for hotspot identification within applications,
 * Atomics classification, and comments :) Indigo is designed for the PIN tool version 3
 * instead of the older version of PIN used by SST and is compatible with newer
 * linux kernel version. The remaining mechanisms are taken
 * from the SST toolset.
 *
 * Current issues:
 * 1) LIBZ support is not supported due to PIN limitations.
 * One possible fix to this issue is to compile the libz with
 * the pintool instead of trying to include as a library.
 *
 * 2) Atomify currently is thread agnostic meaning that if one thread
 * enables atomify it is enabled for all threads which leads to additional
 * instructions being classified as atomics. The GAP benchmarks are relatively well
 * ballanced due to OpenMP so this is a minor issue but Galois benchmarks are prone
 * to over "atomomification". The solution to this problem is to identify the thread
 * id calling the atomify function similar to the way that tracing is enabled.
 *
 * 3) Binary output of traces is currently not supported due some inconsistencies between
 * the prospero core and the genarated trace. This issue needs more investigation.
 */ 

/**
  * updates
  * [11/28/17] add the data compression
  * [08/15/17] add the periodic recording feature, seokin
  * [08/20/17] fixed the LIBZ issue, seokin
  */

#include <sst_config.h>
#include "pin.H"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <iostream>
#include <inttypes.h>
#include "atomichandler.h"
#include <map>
#include <vector>

//#define HAVE_LIBZ 1
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif
//#include "/home/mbhealy/Workspace/Tools/zlib-1.2.11/install/include/zlib.h"
//#include <zlib.h>

//#define COMP_DEBUG 1
using namespace std;

uint32_t max_thread_count;
uint32_t trace_format;
uint64_t instruction_count;
uint32_t traceEnabled __attribute__((aligned(64)));
uint32_t atomifyEnabled __attribute__((aligned(64)));
uint64_t nextFileTrip;

// atomic instructions profiling and pim levels
uint32_t atomicProfileLevel;
uint32_t pimSupportLevel;

uint64_t simpointInterval=0;		//the number of instructions between simulation points
uint64_t numSimInst=0;		//the number of instructions executed in the simulation points
uint64_t numSimpoint=0;		//the number of simulation points
uint32_t compressionRecordEnabled=0;
uint64_t numcopyfail=0;
uint32_t similarity_distance=0;
uint64_t g_similarity_incomp_cnt=0;
uint32_t g_similarity_comp_cnt=0;
uint64_t g_accessed_cacheline_num=0;

const char READ_OPERATION_CHAR = 'R';
const char WRITE_OPERATION_CHAR = 'W';

char RECORD_BUFFER[ sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(char) ];

// We have two file pointers, one for compressed traces and one for
// "normal" (binary or text) traces
FILE** trace;

#ifdef HAVE_LIBZ
gzFile* traceZ;
#endif

typedef struct {
	UINT64 threadInit;
	UINT64 insCount;
	UINT64 readCount;
	UINT64 writeCount;
	UINT64 atomicCount;
	UINT64 currentFile;
	UINT64 atomicDrift;
	UINT64 padD;
	UINT64 padE;
	UINT64 padF;
	UINT64 tmpInstCnt;
	UINT64 simpointCnt;
	UINT64 maddr;
        UINT32 msize;
        bool flagRecord;
	bool done;
} threadRecord;

char** fileBuffers;
threadRecord* thread_instr_id;

KNOB<string> KnobInsRoutine(KNOB_MODE_WRITEONCE, "pintool",
		"r", "", "Instrument only a specific routine (if not specified all instructions are instrumented");
KNOB<string> KnobTraceFile(KNOB_MODE_WRITEONCE, "pintool",
		"o", "sstprospero", "Output analysis to trace file.");
KNOB<string> KnobTraceFormat(KNOB_MODE_WRITEONCE, "pintool",
		"f", "text", "Output format, \'text\' = Plain text, \'binary\' = Binary, \'compressed\' = zlib compressed");
KNOB<UINT32> KnobMaxThreadCount(KNOB_MODE_WRITEONCE, "pintool",
		"t", "1", "Maximum number of threads to record memory patterns");
KNOB<UINT32> KnobFileBufferSize(KNOB_MODE_WRITEONCE, "pintool",
		"b", "32768", "Size in bytes for each trace buffer");
KNOB<UINT32> KnobTraceEnabled(KNOB_MODE_WRITEONCE, "pintool",
		"m", "0", "Disable until application says that tracing can start, 0=disable until app, 1=start enabled, default=1");
KNOB<UINT64> KnobFileTrip(KNOB_MODE_WRITEONCE, "pintool",
		"l", "1125899906842624", "Trip into a new trace file at this instruction count, default=1125899906842624 (2**50)");
KNOB<UINT32> TrapAtomicProfile(KNOB_MODE_WRITEONCE, "pintool",
		"a", "0", "Atomic instruction profiling level (0 = disabled, 1 = enabled)");
KNOB<UINT32> TrapPIMSupport(KNOB_MODE_WRITEONCE, "pintool",
		"p", "0", "PIM support level (0 = disabled, 1 = enabled)");
KNOB<UINT64> KnobSimpointInterval(KNOB_MODE_WRITEONCE, "pintool",
    "i", "0", "The number of instructions between simulation points, default=0");
KNOB<UINT64> KnobNumSimInst(KNOB_MODE_WRITEONCE, "pintool",
    "s", "1125899906842624", "The number of instructions executed in the simulation points, default=1125899906842624");
KNOB<UINT64> KnobNumSimpoint(KNOB_MODE_WRITEONCE, "pintool",
    "n", "1125899906842624", "The number of the simulation points, default=1125899906842624");
KNOB<UINT32> KnobContentRecord(KNOB_MODE_WRITEONCE, "pintool",
    "c", "0", "Record the compression ratio of memory contents (0 = disabled, 1 = enabled), default=0");




/** Called when tracing should be enabled.
 * This function replaces the function within
 * the application source code. When enabled
 * the trace tools write the memory instruction
 * trace to the output file per thread.
 */

void indigo_enable() {
#ifdef Indigo_DEBUG
	printf("Indigo: Tracing enabled\n");
#endif
	traceEnabled = 1;
}

/** Called when tracing should be disabled.
 * This function replaces the function within
 * the application source code. When disabled the
 * application continues executing but the instruction
 * trace output is disabled.
 */

void indigo_disable() {
#ifdef Indigo_DEBUG
	printf("Indigo: Tracing disabled.\n");
#endif
	traceEnabled = 0;
}

/** Called when "atomification" should be enabled.
 * This function replaces the function within
 * the application source code. When enabled the
 * regular instructions are changed to ATOMIC_ATOMIFY
 * and the prospero core can handle them appropriately.
 */

void indigo_atomify_enable() {
#ifdef Indigo_DEBUG
	printf("Indigo: Atomify enabled\n");
#endif
	atomifyEnabled = 1;
}

/** Called when "atomification" should be disabled.
 * This function replaces the function within
 * the application source code.
 */

void indigo_atomify_disable() {
#ifdef Indigo_DEBUG
	printf("Indigo: Atomify disabled.\n");
#endif
	atomifyEnabled = 0;
}

bool is_atomic(int opcode){
	return (opcode == NON_ATOMIC) ? false : true;
}


/**
 *  Mapping of PIN instruction opcodes to ATOMIC opcodes.
 */
uint32_t decode_atomic(std::string instCode){
	if (instCode == "ADD_LOCK"){
		return ATOMIC_ADD;
	}
	else if (instCode == "ADC_LOCK"){
		return ATOMIC_ADC;
	}
	else if (instCode == "AND_LOCK"){
		return ATOMIC_AND;
	}
	else if (instCode == "BTC_LOCK"){
		return ATOMIC_BTC;
	}
	else if (instCode == "BTR_LOCK"){
		return ATOMIC_BTR;
	}
	else if (instCode == "BTS_LOCK"){
		return ATOMIC_BTS;
	}
	else if (instCode == "XCHG_LOCK"){
		return ATOMIC_XCHG;
	}
	else if (instCode == "CMPXCHG_LOCK" || instCode == "CMPXCHG8B_LOCK" || instCode == "CMPXCHG16B_LOCK"){
		return ATOMIC_CMPXCHG;
	}
	else if (instCode == "DEC_LOCK"){
		return ATOMIC_DEC;
	}
	else if (instCode == "INC_LOCK"){
		return ATOMIC_INC;
	}
	else if (instCode == "NEG_LOCK"){
		return ATOMIC_NEG;
	}
	else if (instCode == "NOT_LOCK"){
		return ATOMIC_NOT;
	}
	else if (instCode == "OR_LOCK"){
		return ATOMIC_OR;
	}
	else if (instCode == "SBB_LOCK"){
		return ATOMIC_SBB;
	}
	else if (instCode == "SUB_LOCK"){
		return ATOMIC_SUB;
	}
	else if (instCode == "XOR_LOCK"){
		return ATOMIC_XOR;
	}
	else if (instCode == "XADD_LOCK"){
		return ATOMIC_XADD;
	}
	else{
		return NON_ATOMIC;
	}
}

void copy(VOID* dest, const VOID* source, int destoffset, int count) {
	char* dest_c = (char*) dest;
	char* source_c = (char*) source;
	int counter;

	for(counter = 0; counter < count; counter++) {
		dest_c[destoffset + counter] = source_c[counter];
	}
}


/**
 * Start the instruction counter when a thread begins tracing. 
 */
VOID PerformInstrumentCountCheck(THREADID id) {

#ifdef Indigo_DEBUG
	printf("Indigo: Calling into PerformInstrumentCountCheck...\n");
#endif
	// in some cases the thread id maybe higher than the number of monitoring threads
	if(id >= max_thread_count) {
		return;
	}

	if(thread_instr_id[id].threadInit == 0) {
		printf("Indigo: Thread[%d] starts at instruction %lu\n", id, thread_instr_id[0].insCount);
		// Copy over instructions from thread zero and mark started;
		thread_instr_id[id].insCount = thread_instr_id[0].insCount;
		thread_instr_id[id].threadInit = 1;
	}
}


/**
 * Data compression
 */
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

int64_t getSignedExtension(int64_t data, uint32_t size) {
	         assert(size<=65);
	         int64_t new_data;
	         new_data=(data<<(64-size))>>(64-size);
	         return new_data;
}

uint32_t getCompressedSize(uint8_t *cacheline) {


	int min_compressed_size = 512;


	std::vector<uint64_t> data_vec;

	std::vector<uint32_t> base_size(3);
	std::map<uint32_t, uint32_t> min_delta_size_map;
	std::map<uint32_t, uint64_t> min_base_map;
	std::map<uint32_t, uint32_t> min_compressed_size_map;
	base_size = {2, 4, 8}; //

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
#ifdef COMP_DEBUG
					printf("k:%d base: %llx max_delta_size_base: %d max_delta_size_immd     : %d num_delta_base:%d num_delta_immd:%d compressed_size:%d compression ratio:%lf\n",
			                                k, base, max_delta_size_base, max_delta_size_immd, delta_base_vector.size(),
			                                                               delta_immd_vector.size(), compressed_size, (double) 512 / (double) compressed_size);
#endif

			//get min compressed size
			if (compressed_size < min_compressed_size) {
				min_k = k;
				min_base = base;
				min_compressed_size = compressed_size;

                            //validate compression algorithm
                            if (1) {
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
 //                                                   data_vec[data_idx], data);
                                    if (data != data_vec[data_idx++]) {
                                        printf("decompression error\n");
                                        exit(1);
                                    }
                                }
                            }
			}
		}
	}
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




/** 
 * Read the content of a cacheline
 */

VOID ReadCacheLine(uint64_t addr, uint64_t * data)
{
	//assume that cache line size is 64B
	uint64_t addr_new = (addr>>6)<<6;
	int copied_size= PIN_SafeCopy((uint8_t*)data, (VOID*) addr_new, 64);
	if(copied_size !=64)
		fprintf(stderr, "indigo memory copy fail\n");


	//checkSimilarity(data,addr);
#ifdef COMP_DEBUG
        for(int i=0;i<8;i++)
        {

            fprintf(stderr,"[PINTOOL] cacheline read [%d] addr:%llx data:%llx \n", i, addr_new+i*8, *(data+i));
        }
#endif

}


/** 
 * Print a memory read record
 */

VOID RecordMemRead(VOID * addr, UINT32 size, THREADID thr, UINT32 atomicClass)
{
	if( traceEnabled == 0 || thr >= max_thread_count){
		return;
	}
	
	if(thread_instr_id[thr].flagRecord==true)
	{
#ifdef Indigo_DEBUG
	printf("Indigo: Calling into RecordMemRead...\n");
#endif
	
		UINT64 ma_addr = (UINT64) addr;
        
        UINT64 data=0;
       
        PerformInstrumentCountCheck(thr);

        if(atomifyEnabled && atomicProfileLevel > 1){
            // all non-atomics are "atomified"
            if (atomicClass == NON_ATOMIC){
                atomicClass = ATOMIC_ATOMIFY;
            }
        }

		if(compressionRecordEnabled) {
            uint64_t *cl_data = (uint64_t *) malloc(sizeof(uint64_t) * 8);
            ReadCacheLine(ma_addr, cl_data);
            data=getCompressedSize((uint8_t*)cl_data);


    #ifdef COMP_DEBUG
            for (int i = 0; i < 8; i++) {
                fprintf(stderr,"[PINTOOL] [%d] read addr:%llx data:%llx\n", i, ma_addr, *(cl_data+i));
            }
            fprintf(stderr,"[PINTOOL] compressed size:%d\n",data);
    #endif

        //	if(similarity_distance>0)
        //		checkSimilarity((uint64_t*)ac.inst.data,ac.inst.addr);
            free(cl_data);
		}



	if(0 == trace_format) {
		if(atomicProfileLevel > 0) {
			fprintf(trace[thr], "%llu R %llu %u %llu %u\n",
					(unsigned long long int) thread_instr_id[thr].insCount,
					(unsigned long long int) ma_addr,
					(unsigned int) size,
                                        (unsigned long long) data,
					(unsigned int) atomicClass);
			is_atomic(atomicClass) ? thread_instr_id[thr].atomicCount++ : 0;
		}
		else{
			fprintf(trace[thr], "%llu R %llu %u %llu %u\n",
					(unsigned long long int) thread_instr_id[thr].insCount,
					(unsigned long long int) ma_addr,
					(unsigned int) size,
                                        (unsigned long long) data,
					(unsigned int) NON_ATOMIC);
		}
		thread_instr_id[thr].readCount++;
	} 
	else if (1 == trace_format || 2 == trace_format) {
		copy(RECORD_BUFFER, &(thread_instr_id[thr].insCount), 0, sizeof(UINT64) );
		copy(RECORD_BUFFER, &READ_OPERATION_CHAR, sizeof(UINT64), sizeof(char) );
		copy(RECORD_BUFFER, &ma_addr, sizeof(UINT64) + sizeof(char), sizeof(UINT64) );
		copy(RECORD_BUFFER, &size, sizeof(UINT64) + sizeof(char) + sizeof(UINT64), sizeof(UINT32) );
		copy(RECORD_BUFFER, &data, sizeof(UINT64) + sizeof(char)+sizeof(UINT64)+sizeof(UINT32), sizeof(UINT64) );

#ifdef Indigo_DEBUG_IC
		printf("Indigo: Writing R Instruction Count Core[%d] : %lu \n", thr, thread_instr_id[thr].insCount);
#endif
		if(atomicProfileLevel > 0) {
			is_atomic(atomicClass) ? thread_instr_id[thr].atomicCount++ : 0;
		}
		else{
			atomicClass=NON_ATOMIC;
		}
		
		copy(RECORD_BUFFER, &atomicClass, sizeof(UINT64) + sizeof(char) + sizeof(UINT64) + sizeof(UINT32)+sizeof(UINT64), sizeof(UINT32));

		if(1 == trace_format) {
			fwrite(RECORD_BUFFER, sizeof(UINT64) + sizeof(char) + sizeof(UINT64) + sizeof(UINT32)+ sizeof(UINT64) + sizeof(UINT32), 1, trace[thr]);
		} 
		else {
#ifdef HAVE_LIBZ
			gzwrite(traceZ[thr], RECORD_BUFFER, sizeof(UINT64) + sizeof(char) + sizeof(UINT64) + sizeof(UINT32)+sizeof(UINT64) + sizeof(UINT32));
#endif
		}
		thread_instr_id[thr].readCount++;
	}

#ifdef Indigo_DEBUG
	printf("Indigo: Completed into RecordMemRead...\n");
#endif
	}

}

/** 
 * Print a memory write record
 */



VOID RecordMemAddrSize(VOID* addr, UINT32 size, THREADID thr)
{
    thread_instr_id[thr].maddr=(UINT64) addr;
    thread_instr_id[thr].msize=size;
 //   printf("addrsize: addr:%lld size:%ld\n",addr,size);
}

VOID RecordMemWrite(THREADID thr,  UINT32 atomicClass)
{
        if( traceEnabled == 0 || thr >= max_thread_count){
		return;
	}

	if(thread_instr_id[thr].flagRecord==true)
	{
#ifdef Indigo_DEBUG
	printf("Indigo: Calling into RecordMemWrite...\n");
#endif

		UINT64 ma_addr = thread_instr_id[thr].maddr;
        UINT32 size = thread_instr_id[thr].msize;
		UINT64 data = 0;


		PerformInstrumentCountCheck(thr);

		if(atomifyEnabled && atomicProfileLevel > 1){
			// all non-atomics are "atomified"
			if (atomicClass == NON_ATOMIC){
				atomicClass = ATOMIC_ATOMIFY;
			}
		}

		if(compressionRecordEnabled) {
            uint64_t *cl_data = (uint64_t *) malloc(sizeof(uint64_t) * 8);
            ReadCacheLine(ma_addr, cl_data);
            data=getCompressedSize((uint8_t*)cl_data);


    #ifdef COMP_DEBUG
            for (int i = 0; i < 8; i++) {
                fprintf(stderr,"[PINTOOL] [%d] read addr:%llx data:%llx\n", i, ma_addr, *(cl_data+i));
            }
            fprintf(stderr,"[PINTOOL] compressed size:%d\n",data);
    #endif

        //	if(similarity_distance>0)
        //		checkSimilarity((uint64_t*)ac.inst.data,ac.inst.addr);
            free(cl_data);
		}



	if(0 == trace_format) {
		if(atomicProfileLevel > 0) {
			fprintf(trace[thr], "%llu W %llu %u %llu %u\n",
					(unsigned long long int) thread_instr_id[thr].insCount,
					(unsigned long long int) ma_addr,
					(unsigned int) size,
                                        (unsigned long long int) data,
					(unsigned int) atomicClass);
			is_atomic(atomicClass) ? thread_instr_id[thr].atomicCount++ : 0;
		}
		else{
			fprintf(trace[thr], "%llu W %llu %u %llu %u\n",
					(unsigned long long int) thread_instr_id[thr].insCount,
					(unsigned long long int) ma_addr,
					(unsigned int) size,
                                        (unsigned long long int) data,
					(unsigned int) NON_ATOMIC);
		}
		thread_instr_id[thr].writeCount++;
	} 
	
	else if (1 == trace_format || 2 == trace_format) {
		copy(RECORD_BUFFER, &(thread_instr_id[thr].insCount), 0, sizeof(UINT64) );
		copy(RECORD_BUFFER, &WRITE_OPERATION_CHAR, sizeof(UINT64), sizeof(char) );
		copy(RECORD_BUFFER, &ma_addr, sizeof(UINT64) + sizeof(char), sizeof(UINT64) );
		copy(RECORD_BUFFER, &size, sizeof(UINT64) + sizeof(char) + sizeof(UINT64), sizeof(UINT32) );
		copy(RECORD_BUFFER, &data, sizeof(UINT64) + sizeof(char) + sizeof(UINT64)+sizeof(UINT32), sizeof(UINT64) );

#ifdef Indigo_DEBUG_IC
		printf("Indigo: Writing R Instruction Count Core[%d] : %lu \n", thr, thread_instr_id[thr].insCount);
#endif
		if(atomicProfileLevel > 0) {
			is_atomic(atomicClass) ? thread_instr_id[thr].atomicCount++ : 0;
		}
		else{
			atomicClass=NON_ATOMIC;
		}
	
		copy(RECORD_BUFFER, &atomicClass, sizeof(UINT64) + sizeof(char) + sizeof(UINT64) + sizeof(UINT32)+sizeof(UINT64), sizeof(UINT32));

		if(1 == trace_format) {
                    if(thr < max_thread_count && (traceEnabled >0)){
			    fwrite(RECORD_BUFFER, sizeof(UINT64) + sizeof(char) + sizeof(UINT64) + sizeof(UINT32) + sizeof(UINT64)+sizeof(UINT32), 1, trace[thr]);
                            thread_instr_id[thr].writeCount++;
                        }
		}
		else {
#ifdef HAVE_LIBZ

                    if(thr < max_thread_count && (traceEnabled >0)){
			gzwrite(traceZ[thr], RECORD_BUFFER, sizeof(UINT64) + sizeof(char) + sizeof(UINT64) + sizeof(UINT32) + sizeof(UINT64)+sizeof(UINT32));
                        thread_instr_id[thr].writeCount++;
                        }
#endif
		}
		
		thread_instr_id[thr].writeCount++;
	}

#ifdef Indigo_DEBUG
	printf("Indigo: Completed into RecordMemWrite...\n");
#endif
	}
}

VOID IncrementInstructionCount(THREADID id) {
	if (traceEnabled>0 && id < max_thread_count){

#ifdef Indigo_DEBUG
	printf("Indigo: Increment Instruction Count Core[%d] : %lu \n", id, thread_instr_id[id].insCount);
#endif
		thread_instr_id[id].insCount++;

		if(id==0) {
			if (simpointInterval == 0) {
				for(int i=0;i<max_thread_count;i++)
					thread_instr_id[i].flagRecord = true;

			} else if (thread_instr_id[id].flagRecord == false) {
				if (thread_instr_id[id].tmpInstCnt < simpointInterval) {
						thread_instr_id[id].tmpInstCnt++;
				}
				else {
					if (thread_instr_id[id].simpointCnt < numSimpoint) {
						for(int i=0;i<max_thread_count;i++)
							thread_instr_id[i].flagRecord = true;

						thread_instr_id[id].tmpInstCnt = 0;
						fprintf(stderr, "Indigo: Start recording.. InstCnt:%llu\n", thread_instr_id[id].insCount);
						thread_instr_id[id].simpointCnt++;
					}
					else
					{

						/*thread_instr_id[id].done = true;
						unsigned int done_cnt = 0;
						for (unsigned int i = 0; i < max_thread_count; i++) {
							if (thread_instr_id[i].done == true)
								done_cnt++;
						}
						*/

						//if (done_cnt == max_thread_count)
							PIN_ExitApplication(1);
					}
				}
			} else {
				if (thread_instr_id[id].tmpInstCnt < numSimInst)
					thread_instr_id[id].tmpInstCnt++;
				else {

					for(int i=0;i<max_thread_count;i++)
						thread_instr_id[i].flagRecord = false;
					thread_instr_id[id].tmpInstCnt = 0;
					printf("Indigo: Pause recording... InstCnt:%llu\n", thread_instr_id[id].insCount);
				}
			}
		}

        /*if(simpointInterval ==0)
        {
			thread_instr_id[id].flagRecord=true;
		}
		else if(thread_instr_id[id].flagRecord==false)
		{
			if(thread_instr_id[id].tmpInstCnt< simpointInterval)
			{
				thread_instr_id[id].tmpInstCnt++;
			}
			else
			{
				if(thread_instr_id[id].simpointCnt<numSimpoint)
				{
					thread_instr_id[id].flagRecord=true;
					thread_instr_id[id].tmpInstCnt=0;
					fprintf(stderr,"Indigo: Start recording.. InstCnt:%llu\n",thread_instr_id[id].insCount);
					thread_instr_id[id].simpointCnt++;
				}
				else
				{
					thread_instr_id[id].done=true;
					unsigned int done_cnt=0;
					for(unsigned int i=0; i<max_thread_count;i++)
					{
						if(thread_instr_id[i].done==true)
								done_cnt++;
					}
						
					if(done_cnt==max_thread_count)
						PIN_ExitApplication(1);
				}
			}
			
		}
		else
		{	
			if(thread_instr_id[id].tmpInstCnt < numSimInst)
				thread_instr_id[id].tmpInstCnt++;
			else
			{
				thread_instr_id[id].flagRecord=false;
				thread_instr_id[id].tmpInstCnt=0;
				printf("Indigo: Pause recording... InstCnt:%llu\n",thread_instr_id[id].insCount);
			}
		}*/
		

		if(thread_instr_id[id].insCount >= (nextFileTrip * thread_instr_id[id].currentFile)) {
			char buffer[256];

			if(trace_format == 0 || trace_format == 1) {
				fclose(trace[id]);

				if(trace_format == 0) {
					sprintf(buffer, "%s-%lu-%lu.trace",
							KnobTraceFile.Value().c_str(),
							(unsigned long) id,
							(unsigned long) thread_instr_id[id].currentFile);
					trace[id] = fopen(buffer, "wt");
				} else {
					sprintf(buffer,	"%s-%lu-%lu-bin.trace",
							KnobTraceFile.Value().c_str(),
							(unsigned long) id,
							(unsigned long) thread_instr_id[id].currentFile);
					trace[id] = fopen(buffer, "wb");
				}
			}
#ifdef HAVE_LIBZ
			else if(trace_format == 2) {
				gzclose(traceZ[id]);
				sprintf(buffer, "%s-%lu-%lu-gz.trace",
						KnobTraceFile.Value().c_str(),
						(unsigned long) id,
						(unsigned long) thread_instr_id[id].currentFile);
				traceZ[id] = gzopen(buffer, "wb");
			}
#endif
			thread_instr_id[id].currentFile++;
		}
#ifdef Indigo_DEBUG
	printf("Indigo: Increment Instruction count completeded...\n");
#endif


	}
}

/** Instruction is called for every instruction and instruments reads and writes
*/
VOID Instruction(INS ins, VOID *v)
{
	// Instruments memory accesses using a predicated call, i.e.
	// the instrumentation is called iff the instruction will actually be executed.
	//
	// The IA-64 architecture has explicitly predicated instructions.
	// On the IA-32 and Intel(R) 64 architectures conditional moves and REP
	// prefixed instructions appear as predicated instructions in Pin.
	UINT32 memOperands = INS_MemoryOperandCount(ins);

	// Iterate over each memory operand of the instruction.
	for (UINT32 memOp = 0; memOp < memOperands; memOp++)
	{
		USIZE mem_size = INS_MemoryOperandSize(ins, memOp);
		UINT32 atomicClass = NON_ATOMIC;
		std::string instCode = INS_Mnemonic(ins);

		// check if instruction is an atomic op
		// in IA-32,64 the lock prefix denotes atomics
		if (atomicProfileLevel > 0){
			// if LockPRefix is enabled then it is an atomic instruction
			if(INS_LockPrefix(ins) && instCode.size() > 1){
				atomicClass = decode_atomic(instCode);
				if (atomicClass == NON_ATOMIC){
					std::cerr << "CORE["<< IARG_THREAD_ID <<"] R->"<< std::boolalpha << INS_IsMemoryRead(ins)<<" W->"<< std::boolalpha<< INS_IsMemoryWrite(ins) <<" : " << instCode << std::endl;
					PIN_ERROR( "Unknown atomic instruction opcode.\n");
				}
			}
		}

                if(mem_size>8)
                        numcopyfail++;

		if (INS_MemoryOperandIsRead(ins, memOp))
		{
			INS_InsertPredicatedCall(
					ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
					IARG_MEMORYOP_EA, memOp,
					IARG_UINT32, (UINT32) mem_size,
					IARG_THREAD_ID,
					IARG_UINT32, atomicClass,
					IARG_END);
		}

		// Note that in some architectures a single memory operand can be
		// both read and written (for instance incl (%eax) on IA-32)
		// In that case we instrument it once for read and once for write.
//	if (INS_MemoryOperandIsWritten(ins, memOp))
		if (INS_IsMemoryWrite(ins))
		{

                        INS_InsertPredicatedCall(
                                            ins, IPOINT_BEFORE, (AFUNPTR)RecordMemAddrSize,
                                            IARG_MEMORYWRITE_EA,
                                            IARG_MEMORYWRITE_SIZE,
                                            IARG_THREAD_ID,
                                            IARG_END);



                        if(INS_HasFallThrough(ins))
                        {

                      //      std::cout<<"hasfallthrohg\n";
                            INS_InsertPredicatedCall(
                                            ins, IPOINT_AFTER, (AFUNPTR)RecordMemWrite,
                                            IARG_THREAD_ID,
                                            IARG_UINT32, atomicClass,
                                            IARG_END);
                        }
                        if(INS_IsBranchOrCall(ins))
                        {
                            INS_InsertPredicatedCall(
                                            ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)RecordMemWrite,
                                            IARG_THREAD_ID,
                                            IARG_UINT32, atomicClass,
                                            IARG_END);
                        }

		}
	}

	INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) IncrementInstructionCount, IARG_THREAD_ID, IARG_END);
}

/**
 * InstrumentSpecificRoutine will find and replace Indigo API functions and instument the application
 * source code such that every instruction is profiled.
 */
VOID InstrumentSpecificRoutine(RTN rtn, VOID* v) {
	std::cout << "Indigo: Function: " << RTN_Name(rtn) << std::endl;
	if(RTN_Name(rtn) == "indigo_enable_tracing") {
		std::cout << "Indigo: Found enable routine: " << RTN_Name(rtn) << ", instrumenting for tracing..." << std::endl;
		RTN_Replace(rtn, (AFUNPTR) indigo_enable);
	} else if(RTN_Name(rtn) == "indigo_disable_tracing") {
		std::cout << "Indigo: Found disable routine: " << RTN_Name(rtn) << ", instrumenting for tracing..." << std::endl;
		RTN_Replace(rtn, (AFUNPTR) indigo_disable);
	} else if(RTN_Name(rtn) == "_indigo_enable_tracing") {
		std::cout << "Indigo: Found enable routine: " << RTN_Name(rtn) << ", instrumenting for tracing..." << std::endl;
		RTN_Replace(rtn, (AFUNPTR) indigo_enable);
	} else if(RTN_Name(rtn) == "_indigo_disable_tracing") {
		std::cout << "Indigo: Found disable routine: " << RTN_Name(rtn) << ", instrumenting for tracing..." << std::endl;
		RTN_Replace(rtn, (AFUNPTR) indigo_disable);
	} else if(RTN_Name(rtn) == "indigo_enable_atomify") {
		std::cout << "Indigo: Found enable atomify routine: " << RTN_Name(rtn) << ", instrumenting as atomic..." << std::endl;
		RTN_Replace(rtn, (AFUNPTR) indigo_atomify_enable);
	} else if(RTN_Name(rtn) == "indigo_disable_atomify") {
		std::cout << "Indigo: Found disable atomify routine: " << RTN_Name(rtn) << ", instrumenting as atomic..." << std::endl;
		RTN_Replace(rtn, (AFUNPTR) indigo_atomify_disable);
	} 

	RTN_Open(rtn);

	for(INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
		Instruction(ins, v);
	}

	RTN_Close(rtn);

}

/**
 * Fini is called at the end of tracing and prints statistics for each tracked thread.
 */
VOID Fini(INT32 code, VOID *v)
{
	printf("Indigo: Tracing is complete, closing trace files...\n");
	printf("Indigo: Main thread exists with %lu instructions\n", thread_instr_id[0].insCount);

	if( (0 == trace_format) || (1 == trace_format)) {
		for(UINT32 i = 0; i < max_thread_count; ++i) {
			fclose(trace[i]);
		}
#ifdef HAVE_LIBZ
	} 
	else if (2 == trace_format) {
		for(UINT32 i = 0; i < max_thread_count; ++i) {
			gzclose(traceZ[i]);
		}
#endif
	}

	max_thread_count = KnobMaxThreadCount.Value();
	
	for(UINT32 i = 0; i < max_thread_count; ++i) {
		printf("Indigo: Thread[%d] instr entries:     %lu\n",i, thread_instr_id[i].insCount);
		printf("Indigo: Thread[%d] read entries:     %lu\n",i, thread_instr_id[i].readCount);
		printf("Indigo: Thread[%d] write entries:    %lu\n",i, thread_instr_id[i].writeCount);
		if (atomicProfileLevel > 0){
			printf("Indigo: Thread[%d] atomic entries:    %lu\n",i, thread_instr_id[i].atomicCount);
		}
		printf("Indigo: Thread total instruction: %lu\n", thread_instr_id[i].insCount);
		printf("Indigo: Thread the number of blocks recorded: %lu\n", thread_instr_id[i].simpointCnt);
	}

        printf("Indigo: total number of data copy fail:%llu\n",numcopyfail);

	printf("Indigo: Done.\n");
}

INT32 Usage()
{
	PIN_ERROR( "This Pintool prints a trace of memory read and write addresses for atomic and regular instructions. When enabled, it classifies instrumented instructions as \"atomified\" \n" + KNOB_BASE::StringKnobSummary() + "\n");
	return -1;
}

int main(int argc, char *argv[])
{
	if (PIN_Init(argc, argv)) return Usage();
	PIN_InitSymbols();

	traceEnabled = KnobTraceEnabled.Value();
	atomifyEnabled = 0;

	max_thread_count = KnobMaxThreadCount.Value();
	printf("Indigo: User requests that a maximum of %d threads are instrumented\n", max_thread_count);
	printf("Indigo: File buffer per thread is %d bytes\n", KnobFileBufferSize.Value());

	if(traceEnabled == 0) {
		printf("Indigo: Trace is disabled from startup\n");
	} else {
		printf("Indigo: Trace is enabled from startup\n");
	}

	trace  = (FILE**) malloc(sizeof(FILE*) * max_thread_count);
#ifdef HAVE_LIBZ
	traceZ = (gzFile*) malloc(sizeof(gzFile) * max_thread_count);
#endif
	fileBuffers = (char**) malloc(sizeof(char*) * max_thread_count);

	char nameBuffer[256];

	if(KnobTraceFormat.Value() == "text") {
		printf("Indigo: Tracing will be recorded in text format.ddd\n");
		trace_format = 0;
		for(UINT32 i = 0; i < max_thread_count; ++i) {
			sprintf(nameBuffer, "%s-%lu-0.trace", KnobTraceFile.Value().c_str(), (unsigned long) i);
			trace[i] = fopen(nameBuffer, "wt");
		}

		for(UINT32 i = 0; i < max_thread_count; ++i) {
			fileBuffers[i] = (char*) malloc(sizeof(char) * KnobFileBufferSize.Value());
			setvbuf(trace[i], fileBuffers[i], _IOFBF, (size_t) KnobFileBufferSize.Value());
		}

	} else if(KnobTraceFormat.Value() == "binary") {
		printf("Indigo: Tracing will be recorded in uncompressed binary format.\n");
		trace_format = 1;

		for(UINT32 i = 0; i < max_thread_count; ++i) {
			sprintf(nameBuffer, "%s-%lu-0-bin.trace", KnobTraceFile.Value().c_str(), (unsigned long) i);
			trace[i] = fopen(nameBuffer, "wb");
		}

		for(UINT32 i = 0; i < max_thread_count; ++i) {
			fileBuffers[i] = (char*) malloc(sizeof(char) * KnobFileBufferSize.Value());
			setvbuf(trace[i], fileBuffers[i], _IOFBF, (size_t) KnobFileBufferSize.Value());
		}
#ifdef HAVE_LIBZ
	} else if(KnobTraceFormat.Value() == "compressed") {
		printf("Indigo: Tracing will be recorded in compressed binary format.\n");
		trace_format = 2;

		for(UINT32 i = 0; i < max_thread_count; ++i) {
			sprintf(nameBuffer, "%s-%lu-0-gz.trace", KnobTraceFile.Value().c_str(), (unsigned long) i);
			traceZ[i] = gzopen(nameBuffer, "wb");
		}
#endif
	} else {
		std::cerr << "Error: Unknown trace format: " << KnobTraceFormat.Value() << "." << std::endl;
		exit(-1);
	}

	atomicProfileLevel =  TrapAtomicProfile.Value();

	if(atomicProfileLevel > 0) {
		printf("Indigo: Atomic instructions profile level is configured to: %d\n", atomicProfileLevel);
	} else {
		printf("Indigo: Atomic instructions profiling is disabled\n");
	}

	pimSupportLevel = TrapPIMSupport.Value();

	if(pimSupportLevel > 0) {
		printf("Indigo: PIM support level is configured to: %d\n",pimSupportLevel);
	} else {
		printf("Indigo: PIM support is disabled\n");
	}


	posix_memalign((void**) &thread_instr_id, 64, sizeof(threadRecord) * max_thread_count);
	for(UINT32 i = 0; i < max_thread_count; ++i) {
		thread_instr_id[i].insCount = 0;
		thread_instr_id[i].threadInit = 0;
		thread_instr_id[i].readCount = 0;
		thread_instr_id[i].writeCount = 0;
		thread_instr_id[i].atomicCount = 0;
		thread_instr_id[i].atomicDrift = 0;
		// Next file is going to be marked as 1 (we are really on file 0).
		thread_instr_id[i].currentFile = 1;
		thread_instr_id[i].tmpInstCnt = 0;
		thread_instr_id[i].flagRecord = false;
		thread_instr_id[i].simpointCnt = 0;
		thread_instr_id[i].done=false;
	}

	nextFileTrip = KnobFileTrip.Value();
	printf("Indigo: Next file trip count set to %lu instructions.\n", nextFileTrip);
	
	simpointInterval = KnobSimpointInterval.Value();
        printf("Indigo: The number of instructions between simulation points: %llu\n", simpointInterval);

        numSimInst = KnobNumSimInst.Value();
        printf("Indigo: The number of simulated instructions : %llu\n", numSimInst);
	
	numSimpoint = KnobNumSimpoint.Value();
        printf("Indigo: The number of simulation point: %llu\n", numSimpoint);


        compressionRecordEnabled = KnobContentRecord.Value();
        if(compressionRecordEnabled == 1)
            printf("Indigo: compression ratio of memory content will be recored\n");

	// Thread zero is always started
	thread_instr_id[0].threadInit = 1;

	RTN_AddInstrumentFunction(InstrumentSpecificRoutine, 0);

	PIN_AddFiniFunction(Fini, 0);

	PIN_StartProgram();

	return 0;
}
