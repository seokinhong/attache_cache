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

// SST includes
#include "sst_config.h"

#define SST_ELI_COMPILE_OLD_ELI_WITHOUT_DEPRECATION_WARNINGS

#include "sst/core/element.h"

// local includes
#include "c_MemhBridge.hpp"
#include "c_AddressHasher.hpp"
#include "c_TxnGenSeq.hpp"
#include "c_TxnGenRand.hpp"
#include "c_TracefileReader.hpp"
#include "c_DramSimTraceReader.hpp"
#include "c_USimmTraceReader.hpp"
#include "c_TxnDriver.hpp"
#include "c_TxnConverter.hpp"
#include "c_TxnScheduler.hpp"
#include "c_CmdReqEvent.hpp"
#include "c_CmdResEvent.hpp"
#include "c_CmdDriver.hpp"
#include "c_DeviceDriver.hpp"
#include "c_BankReceiver.hpp"
#include "c_Dimm.hpp"
#include "c_Controller.hpp"
#include "c_TxnDispatcher.hpp"
#include "c_TxnGen.hpp"
#include "c_TraceReader.hpp"
#include "c_MemhBridgeContent.hpp"


// namespaces
using namespace SST;
using namespace SST::n_Bank;
using namespace SST::n_CmdDriver;
using namespace SST::n_TxnDriver;
using namespace SST::n_BankReceiver;
using namespace SST::Statistics;

/*----ALLOCATORS FOR COMPONENTS----*/

// c_MemhBridge
static Component*
create_c_MemhBridge(SST::ComponentId_t id, SST::Params& params) {
	return new c_MemhBridge(id, params);
}
// c_MemhBridge
static Component*
create_c_MemhBridgeContent(SST::ComponentId_t id, SST::Params& params) {
	return new c_MemhBridgeContent(id, params);
}

// c_TxnGenSeq
static Component*
create_c_TxnGen(SST::ComponentId_t id, SST::Params& params) {
	return new c_TxnGen(id, params);
}

// c_TxnGenSeq
static Component*
create_c_TxnGenSeq(SST::ComponentId_t id, SST::Params& params) {
	return new c_TxnGenSeq(id, params);
}

// c_TxnGenRand
static Component*
create_c_TxnGenRand(SST::ComponentId_t id, SST::Params& params){
	return new c_TxnGenRand(id, params);
}

// c_TracefileReader
static Component*
create_c_TracefileReader(SST::ComponentId_t id, SST::Params& params){
	return new c_TracefileReader(id, params);
}

// c_TraceReader
static Component*
create_c_TraceReader(SST::ComponentId_t id, SST::Params& params){
	return new c_TraceReader(id, params);
}

// c_DramSimTraceReader
static Component*
create_c_DramSimTraceReader(SST::ComponentId_t id, SST::Params& params){
	return new c_DramSimTraceReader(id, params);
}

// c_USimmTraceReader
static Component*
create_c_USimmTraceReader(SST::ComponentId_t id, SST::Params& params){
	return new c_USimmTraceReader(id, params);
}

// c_TxnDriver
static Component*
create_c_TxnDriver(SST::ComponentId_t id, SST::Params& params){
	return new c_TxnDriver(id, params);
}


// c_CmdDriver
static Component*
create_c_CmdDriver(SST::ComponentId_t id, SST::Params& params){
	return new c_CmdDriver(id, params);
}


// c_BankReceiver
static Component*
create_c_BankReceiver(SST::ComponentId_t id, SST::Params& params){
	return new c_BankReceiver(id, params);
}

static Component*
create_c_TxnDispatcher(SST::ComponentId_t id, SST::Params& params) {
	return new c_TxnDispatcher(id, params);
}
// c_Dimm
static Component*
create_c_Dimm(SST::ComponentId_t id, SST::Params& params) {
	return new c_Dimm(id, params);
}

// c_Controller
static Component*
create_c_Controller(SST::ComponentId_t id, SST::Params& params) {
	return new c_Controller(id, params);
}
// Address Mapper
static SubComponent*
create_c_AddressHasher(Component * owner, Params& params) {
	return new c_AddressHasher(owner, params);
}

// Transaction Converter
static SubComponent*
create_c_TxnScheduler(Component * owner, Params& params) {
	return new c_TxnScheduler(owner, params);
}

// Transaction Converter
static SubComponent*
create_c_TxnConverter(Component * owner, Params& params) {
	return new c_TxnConverter(owner, params);
}

// Command Scheduler
static SubComponent*
create_c_DeviceDriver(Component * owner, Params& params) {
	return new c_DeviceDriver(owner, params);
}

// Device Controller
static SubComponent*
create_c_CmdScheduler(Component * owner, Params& params) {
	return new c_CmdScheduler(owner, params);
}

static const char* c_TxnDispatcher_port_events[] = { "MemEvent", NULL };

/*----SETUP c_AddressHasher  STRUCTURES----*/
static const ElementInfoParam c_TxnDispatcher_params[] = {
		{"numLanes", "Total number of lanes", NULL},
		{"laneIdxPosition", "Bit posiiton of the lane index in the address.. [End:Start]", NULL},
		{ NULL, NULL, NULL } };


static const ElementInfoPort c_TxnDispatcher_ports[] = {
		{ "txnGen", "link to/from a transaction generator",c_TxnDispatcher_port_events},
		{ "lane_%(lanes)d", "link to/from lanes", c_TxnDispatcher_port_events},
		{ NULL, NULL, NULL } };

/*----SETUP c_AddressHasher  STRUCTURES----*/
static const ElementInfoParam c_AddressHasher_params[] = {
		{"numChannelsPerDimm", "Total number of channels per DIMM", NULL},
		{"numRanksPerChannel", "Total number of ranks per channel", NULL},
		{"numBankGroupsPerRank", "Total number of bank groups per rank", NULL},
		{"numBanksPerBankGroup", "Total number of banks per group", NULL},
		{"numRowsPerBank" "Number of rows in every bank", NULL},
		{"numColsPerBank", "Number of cols in every bank", NULL},
		{"numBytesPerTransaction", "Number of bytes retrieved for every transaction", NULL},
		{"strAddressMapStr","String defining the address mapping scheme",NULL},
		{ NULL, NULL, NULL } };

//static const ElementInfoPort c_AddressHasher_ports[] = {
//		{ NULL, NULL, NULL } };

/*----SETUP c_MemhBridge STRUCTURES----*/
static const ElementInfoParam c_MemhBridge_params[] = {
                {"maxOutstandingReqs", "Maximum number of the outstanding requests", NULL},
		{ NULL, NULL, NULL } };

static const char* c_MemhBridge_mem_port_events[] = { "c_TxnReqEvent","c_TxnResEvent", NULL };
static const char* c_MemhBridge_CPU_events[] = {"c_CPUevent", NULL};

static const ElementInfoPort c_MemhBridge_ports[] = {
		{ "linkCPU", "link to/from CPU",c_MemhBridge_CPU_events},
		{ "lowLink", "link to memory-side components (txn dispatcher or controller)", c_MemhBridge_mem_port_events },
		{ NULL, NULL, NULL } };

static const ElementInfoStatistic c_MemhBridge_stats[] = {
  {"readTxnsSent", "Number of read transactions sent", "reads", 1}, // Name, Desc, Units, Enable Level
  {"writeTxnsSent", "Number of write transactions sent", "writes", 1}, // Name, Desc, Units, Enable Level
  {"readTxnsCompleted", "Number of read transactions completed", "reads", 1}, // Name, Desc, Units, Enable Level
  {"writeTxnsCompleted", "Number of write transactions completed", "writes", 1},
  {"txnsPerCycle", "Transactions Per Cycle", "Txns/Cycle", 1},
  {"readTxnsLatency", "Average latency of read transactions", "cycles", 1},
  {"writeTxnsLatency", "Average latency of write transactions", "cycles", 1},
  {"txnsLatency", "Average latency of (read/write) transactions", "cycles", 1},
  {NULL, NULL, NULL, 0}
};

/*----SETUP c_MemhBridge STRUCTURES----*/
static const char* c_MemhBridgeContent_Content_events[] = {"c_ContentEvent", NULL};

static const ElementInfoPort c_MemhBridgeContent_ports[] = {
        { "linkCPU", "link to/from CPU",c_MemhBridge_CPU_events},
        { "lowLink", "link to memory-side components (txn dispatcher or controller)", c_MemhBridge_mem_port_events },
		{ "contentLink", "link from CPU for receiving memory contents", c_MemhBridgeContent_Content_events},
        { NULL, NULL, NULL } };


/*----SETUP c_TxnGenSeq STRUCTURES----*/
static const ElementInfoParam c_TxnGenSeq_params[] = {
		{"numTxnGenReqQEntries", "Total entries allowed in the Req queue", NULL},
		{"numTxnGenResQEntries", "Total entries allowed in the Res queue", NULL},
		{"numCtrlReqQEntries", "Total entries in the neighbor TxnConverter's Req queue", NULL},
		{"readWriteRatio", "Ratio of read txn's to generate : write txn's to generate", NULL},
		{ NULL, NULL, NULL } };

static const char* c_TxnGenSeq_req_port_events[] = { "c_TxnReqEvent", NULL };
static const char* c_TxnGenSeq_res_port_events[] = { "c_TxnResEvent", NULL };
static const char* c_TxnGenSeq_token_port_events[] = {"c_TokenChgEvent", NULL};

static const ElementInfoPort c_TxnGenSeq_ports[] = {
		{ "outTxnGenReqPtr", "link to c_TxnGen for outgoing req txn", c_TxnGenSeq_req_port_events },
		{ "inCtrlReqQTokenChg", "link to c_TxnGen for incoming req token", c_TxnGenSeq_token_port_events },
		{ "inCtrlResPtr", "link to c_TxnGen for incoming res txn", c_TxnGenSeq_res_port_events },
		{ "outTxnGenResQTokenChg", "link to c_TxnGen for outgoing res token",c_TxnGenSeq_token_port_events },
		{ NULL, NULL, NULL } };

/*----SETUP c_TxnGen STRUCTURES----*/
static const ElementInfoParam c_TxnGen_params[] = {
		{"maxOutstandingReqs", "Maximum number of the outstanding requests", NULL},
		{"numTxnPerCycle", "The number of transactions generated per cycle", NULL},
		{"readWriteRatio", "Ratio of read txn's to generate : write txn's to generate", NULL},
		{ NULL, NULL, NULL } };

static const char* c_TxnGen_port_events[] = { "c_TxnReqEvent", "c_TxnResEvent", NULL };

static const ElementInfoPort c_TxnGen_ports[] = {
		{ "lowLink", "link to memory-side components (txn dispatcher or controller)", c_TxnGen_port_events },
		{ NULL, NULL, NULL } };

static const ElementInfoStatistic c_TxnGen_stats[] = {
  {"readTxnsSent", "Number of read transactions sent", "reads", 1}, // Name, Desc, Units, Enable Level
  {"writeTxnsSent", "Number of write transactions sent", "writes", 1}, // Name, Desc, Units, Enable Level
  {"readTxnsCompleted", "Number of read transactions completed", "reads", 1}, // Name, Desc, Units, Enable Level
  {"writeTxnsCompleted", "Number of write transactions completed", "writes", 1},
  {"txnsPerCycle", "Transactions Per Cycle", "Txns/Cycle", 1},
  {"readTxnsLatency", "Average latency of read transactions", "cycles", 1},
  {"writeTxnsLatency", "Average latency of write transactions", "cycles", 1},
  {"txnsLatency", "Average latency of (read/write) transactions", "cycles", 1},
  {NULL, NULL, NULL, 0}
};

/*----SETUP c_TxnGenRand STRUCTURES----*/
static const ElementInfoParam c_TxnGenRand_params[] = {
		{"numTxnGenReqQEntries", "Total entries allowed in the Req queue", NULL},
		{"numTxnGenResQEntries", "Total entries allowed in the Res queue", NULL},
		{"numCtrlReqQEntries", "Total entries in the neighbor TxnConverter's Req queue", NULL},
		{"readWriteRatio", "Ratio of read txn's to generate : write txn's to generate", NULL},
		{ NULL, NULL, NULL } };

static const char* c_TxnGenRand_req_port_events[] = { "c_TxnReqEvent", NULL };
static const char* c_TxnGenRand_res_port_events[] = { "c_TxnResEvent", NULL };
static const char* c_TxnGenRand_token_port_events[] = {"c_TokenChgEvent", NULL};

static const ElementInfoPort c_TxnGenRand_ports[] = {
		{ "outTxnGenReqPtr", "link to c_TxnGen for outgoing req txn", c_TxnGenRand_req_port_events },
		{ "inCtrlReqQTokenChg", "link to c_TxnGen for incoming req token", c_TxnGenRand_token_port_events },
		{ "inCtrlResPtr", "link to c_TxnGen for incoming res txn", c_TxnGenRand_res_port_events },
		{ "outTxnGenResQTokenChg", "link to c_TxnGen for outgoing res token",c_TxnGenRand_token_port_events },
		{ NULL, NULL, NULL } };

static const ElementInfoStatistic c_TxnGenRand_stats[] = {
  {"readTxnsCompleted", "Number of read transactions completed", "reads", 1}, // Name, Desc, Units, Enable Level
  {"writeTxnsCompleted", "Number of write transactions completed", "writes", 1},
  {NULL, NULL, NULL, 0}
};

/*----SETUP c_TracefileReader STRUCTURES----*/
static const ElementInfoParam c_TracefileReader_params[] = {
		{"numTxnGenReqQEntries", "Total entries allowed in the Req queue", NULL},
		{"numTxnGenResQEntries", "Total entries allowed in the Res queue", NULL},
		{"numCtrlReqQEntries", "Total entries in the neighbor TxnConverter's Req queue", NULL},
		{"traceFile", "Location of trace file to read", NULL},
		{ NULL, NULL, NULL } };

static const char* c_TracefileReader_req_port_events[] = { "c_TxnReqEvent", NULL };
static const char* c_TracefileReader_res_port_events[] = { "c_TxnResEvent", NULL };
static const char* c_TracefileReader_token_port_events[] = {"c_TokenChgEvent", NULL};

static const ElementInfoPort c_TracefileReader_ports[] = {
		{ "outTxnGenReqPtr", "link to c_TxnGen for outgoing req txn", c_TxnGenRand_req_port_events },
		{ "inCtrlReqQTokenChg", "link to c_TxnGen for incoming req token", c_TxnGenRand_token_port_events },
		{ "inCtrlResPtr", "link to c_TxnGen for incoming res txn", c_TxnGenRand_res_port_events },
		{ "outTxnGenResQTokenChg", "link to c_TxnGen for outgoing res token",c_TxnGenRand_token_port_events },
		{ NULL, NULL, NULL } };




/*----SETUP c_TraceReader STRUCTURES----*/
static const ElementInfoParam c_TraceReader_params[] = {
		{"maxOutstandingReqs", "Maximum number of the outstanding requests", NULL},
		{"numTxnPerCycle", "The number of transactions generated per cycle", NULL},
		{"traceFile", "Location of trace file to read", NULL},
		{ NULL, NULL, NULL } };

static const char* c_TraceReader_port_events[] = { "c_TxnReqEvent", "c_TxnResEvent", NULL };

static const ElementInfoPort c_TraceReader_ports[] = {
		{ "lowLink", "link to memory-side components (txn dispatcher or controller)", c_TraceReader_port_events },
		{ NULL, NULL, NULL } };

static const ElementInfoStatistic c_TraceReader_stats[] = {
  {"readTxnsSent", "Number of read transactions sent", "reads", 1}, // Name, Desc, Units, Enable Level
  {"writeTxnsSent", "Number of write transactions sent", "writes", 1}, // Name, Desc, Units, Enable Level
  {"readTxnsCompleted", "Number of read transactions completed", "reads", 1}, // Name, Desc, Units, Enable Level
  {"writeTxnsCompleted", "Number of write transactions completed", "writes", 1},
  {"txnsPerCycle", "Transactions Per Cycle", "Txns/Cycle", 1},
  {"readTxnsLatency", "Average latency of read transactions", "cycles", 1},
  {"writeTxnsLatency", "Average latency of write transactions", "cycles", 1},
  {"txnsLatency", "Average latency of (read/write) transactions", "cycles", 1},
  {NULL, NULL, NULL, 0}
};

/*----SETUP c_DramSimTraceReader STRUCTURES----*/
static const ElementInfoParam c_DramSimTraceReader_params[] = {
		{"numTxnGenReqQEntries", "Total entries allowed in the Req queue", NULL},
		{"numTxnGenResQEntries", "Total entries allowed in the Res queue", NULL},
		{"numCtrlReqQEntries", "Total entries in the neighbor Ctrl's Req queue", NULL},
		{"traceFile", "Location of trace file to read", NULL},
		{ NULL, NULL, NULL } };

static const char* c_DramSimTraceReader_req_port_events[] = { "c_TxnReqEvent", NULL };
static const char* c_DramSimTraceReader_res_port_events[] = { "c_TxnResEvent", NULL };
static const char* c_DramSimTraceReader_token_port_events[] = {"c_TokenChgEvent", NULL};

static const ElementInfoPort c_DramSimTraceReader_ports[] = {
		{ "outTxnGenReqPtr", "link to c_TxnGen for outgoing req txn", c_TxnGenRand_req_port_events },
		{ "inCtrlReqQTokenChg", "link to c_TxnGen for incoming req token", c_TxnGenRand_token_port_events },
		{ "inCtrlResPtr", "link to c_TxnGen for incoming res txn", c_TxnGenRand_res_port_events },
		{ "outTxnGenResQTokenChg", "link to c_TxnGen for outgoing res token",c_TxnGenRand_token_port_events },
		{ NULL, NULL, NULL } };

static const ElementInfoStatistic c_DramSimTraceReader_stats[] = {
  {"readTxnsCompleted", "Number of read transactions completed", "reads", 1}, // Name, Desc, Units, Enable Level
  {"writeTxnsCompleted", "Number of write transactions completed", "writes", 1},
  {NULL, NULL, NULL, 0}
};

/*----SETUP c_USimmTraceReader STRUCTURES----*/
static const ElementInfoParam c_USimmTraceReader_params[] = {
  {"numTxnGenReqQEntries", "Total entries allowed in the Req queue", NULL},
  {"numTxnGenResQEntries", "Total entries allowed in the Res queue", NULL},
  {"numCtrlReqQEntries", "Total entries in the neighbor Ctrl's Req queue", NULL},
  {"traceFile", "Location of trace file to read", NULL},
  { NULL, NULL, NULL } };

static const char* c_USimmTraceReader_req_port_events[] = { "c_TxnReqEvent", NULL };
static const char* c_USimmTraceReader_res_port_events[] = { "c_TxnResEvent", NULL };
static const char* c_USimmTraceReader_token_port_events[] = {"c_TokenChgEvent", NULL};

static const ElementInfoPort c_USimmTraceReader_ports[] = {
  { "outTxnGenReqPtr", "link to c_TxnGen for outgoing req txn", c_TxnGenRand_req_port_events },
  { "inCtrlReqQTokenChg", "link to c_TxnGen for incoming req token", c_TxnGenRand_token_port_events },
  { "inCtrlResPtr", "link to c_TxnGen for incoming res txn", c_TxnGenRand_res_port_events },
  { "outTxnGenResQTokenChg", "link to c_TxnGen for outgoing res token",c_TxnGenRand_token_port_events },
  { NULL, NULL, NULL } };



/*----SETUP c_TxnDriver STRUCTURES----*/
static const ElementInfoParam c_TxnDriver_params[] = {
		{"numTxnDrvBufferQEntries", "Total entries allowed in the buffer", NULL},
		{"numTxnGenResQEntries", "Total entries allowed in the Res queue of the Txn Driver", NULL},
		{ NULL, NULL, NULL } };


static const char* c_TxnDriver_req_port_events[] = { "c_TxnReqEvent", NULL };
static const char* c_TxnDriver_res_port_events[] = { "c_TxnResEvent", NULL };
static const char* c_TxnDriver_token_port_events[] = {"c_TokenChgEvent", NULL};
static const char* c_TxnDriver_txnreqtoken_port_events[] = {"c_TxnReqTokenChgEvent", NULL};

static const ElementInfoPort c_TxnDriver_ports[] = {
		{ "outTxnDrvReqQTokenChg", "link to c_TxnDriver for outgoing req token", c_TxnDriver_txnreqtoken_port_events },
		{ "inTxnGenReqPtr", "link to c_TxnGen for incoming req txn", c_TxnGenRand_req_port_events },
		{ "inTxnGenResQTokenChg", "link to c_TxnGen for incoming res token", c_TxnGenRand_token_port_events },
		{ "outTxnDrvResPtr", "link to c_TxnGen for outgoing res txn",c_TxnGenRand_res_port_events },
		{ NULL, NULL, NULL } };


/*----SETUP c_TxnScheduler STRUCTURES----*/
static const ElementInfoParam c_TxnScheduler_params[] = {
		{"txnSchedulingPolicy", "Transaction scheduling policy", NULL},
		{"numTxnQEntries", "The number of transaction queue entries", NULL},
		{"queuePolicy", "Read/Write queue selection policy", NULL},
		{NULL, NULL, NULL } };

static const ElementInfoStatistic c_TxnScheduler_stats[] = {
		{NULL, NULL, NULL, 0}
};

/*----SETUP c_TxnConverter STRUCTURES----*/
static const ElementInfoParam c_TxnConverter_params[] = {
		{"relCommandWidth", "Relative width of each command", NULL},
		{"bankPolicy", "Select which bank policy to model", NULL},
		{"boolUseReadA", "Whether to use READ or READA Cmds", NULL},
		{"boolUseWriteA", "Whether to use WRITE or WRITEA Cmds", NULL},
		{NULL, NULL, NULL } };

static const ElementInfoStatistic c_TxnConverter_stats[] = {
  {"readTxnsRecvd", "Number of read transactions received", "reads", 1}, // Name, Desc, Units, Enable Level
  {"writeTxnsRecvd", "Number of write transactions received", "writes", 1},
  {"totalTxnsRecvd", "Number of write transactions received", "transactions", 1},
  {"reqQueueSize", "Total size of the request queue over time", "transactions", 1},
  {"resQueueSize", "Total size of the response queue over time", "transactions", 1},
  {NULL, NULL, NULL, 0}
};


/*----SETUP c_CmdScheduler STRUCTURES----*/
static const ElementInfoParam c_CmdScheduler_params[] = {
		{"numCmdQEntries", "The number of entries in command scheduler's command queue"},
		{NULL, NULL, NULL } };

static const ElementInfoStatistic c_CmdScheduler_stats[] = {
		{NULL, NULL, NULL, 0}
};


/*----SETUP c_CmdDriver STRUCTURES----*/
static const ElementInfoParam c_CmdDriver_params[] = {
		{"numCmdReqQEntries", "Total number of entries in Driver's buffer", NULL},
		{"numTxnResQEntries", "Total number of entries in neighbor TxnConverter's Res queue", NULL},
		{NULL, NULL, NULL } };

static const char* c_CmdDriver_cmdRes_port_events[] = { "c_CmdResEvent", NULL };
static const char* c_CmdDriver_cmdReq_port_events[] = { "c_CmdPtrPkgEvent", NULL };
static const char* c_CmdDriver_token_port_events[] = {"c_TokenChgEvent", NULL};

static const ElementInfoPort c_CmdDriver_ports[] = {
		{"outCmdDrvReqQTokenChg", "link to c_CmdDriver for outgoing req txn token", c_CmdDriver_token_port_events},
		{"inTxnCvtReqPtr", "link to c_CmdDriver for incoming req cmds", c_CmdDriver_cmdReq_port_events},
		{"outCmdDrvResPtr", "link to c_CmdDriver for outgoing res txn", c_CmdDriver_cmdRes_port_events},
		{NULL, NULL, NULL}
};


/*----SETUP c_DeviceDriver STRUCTURES----*/
static const ElementInfoParam c_DeviceDriver_params[] = {
		{"numChannels", "Total number of channels per DIMM", NULL},
		{"numPChannelsPerChannel", "Total number of channels per pseudo channel (added to support HBM)", NULL},
		{"numRanksPerChannel", "Total number of ranks per (p)channel", NULL},
		{"numBankGroupsPerRank", "Total number of bank groups per rank", NULL},
		{"numBanksPerBankGroup", "Total number of banks per group", NULL},
		{"numRowsPerBank" "Number of rows in every bank", NULL},
		{"numColsPerBank", "Number of cols in every bank", NULL},
		{"boolPrintCmdTrace", "Print a command trace", NULL},
		{"strCmdTraceFile", "Filename to print the command trace, or - for stdout", NULL},
		{"boolAllocateCmdResACT", "Allocate space in DeviceDriver Res Q for ACT Cmds", NULL},
		{"boolAllocateCmdResREAD", "Allocate space in DeviceDriver Res Q for READ Cmds", NULL},
		{"boolAllocateCmdResREADA", "Allocate space in DeviceDriver Res Q for READA Cmds", NULL},
		{"boolAllocateCmdResWRITE", "Allocate space in DeviceDriver Res Q for WRITE Cmds", NULL},
		{"boolAllocateCmdResWRITEA", "Allocate space in DeviceDriver Res Q for WRITEA Cmds", NULL},
		{"boolAllocateCmdResPRE", "Allocate space in DeviceDriver Res Q for PRE Cmds", NULL},
		{"boolUseRefresh", "Whether to use REF or not", NULL},
		{"boolDualCommandBus", "Whether to use dual command bus (added to support HBM)", NULL},
		{"boolMultiCycleACT", "Whether to use multi-cycle (two cycles) active command (added to support HBM)", NULL},
		{"nRC", "Bank Param", NULL},
		{"nRRD", "Bank Param", NULL},
		{"nRRD_L", "Bank Param", NULL},
		{"nRRD_S", "Bank Param", NULL},
		{"nRCD", "Bank Param", NULL},
		{"nCCD", "Bank Param", NULL},
		{"nCCD_L", "Bank Param", NULL},
		{"nCCD_L_WR", "Bank Param", NULL},
		{"nCCD_S", "Bank Param", NULL},
		{"nAL", "Bank Param", NULL},
		{"nCL", "Bank Param", NULL},
		{"nCWL", "Bank Param", NULL},
		{"nWR", "Bank Param", NULL},
		{"nWTR", "Bank Param", NULL},
		{"nWTR_L", "Bank Param", NULL},
		{"nWTR_S", "Bank Param", NULL},
		{"nRTW", "Bank Param", NULL},
		{"nEWTR", "Bank Param", NULL},
		{"nERTW", "Bank Param", NULL},
		{"nEWTW", "Bank Param", NULL},
		{"nERTR", "Bank Param", NULL},
		{"nRAS", "Bank Param", NULL},
		{"nRTP", "Bank Param", NULL},
		{"nRP", "Bank Param", NULL},
		{"nRFC", "Bank Param", NULL},
		{"nREFI", "Bank Param", NULL},
		{"nFAW", "Bank Param", NULL},
		{"nBL", "Bank Param", NULL},
		{NULL, NULL, NULL } };

static const ElementInfoStatistic c_DeviceDriver_stats[] = {
  //{"rowHits", "Number of DRAM page buffer hits", "hits", 1}, // Name, Desc, Units, Enable Level
  {NULL,NULL,NULL,0}
};


/*----SETUP c_Controller STRUCTURES----*/
static const ElementInfoParam c_Controller_params[] = {
		{"AddrHasher", "address hasher", "CramSim.c_AddressHasher"},
		{"TxnScheduler", "Transaction Scheduler", "CramSim.c_TxnScheduler"},
		{"TxnConverter", "Transaction Converter", "CramSim.c_TxnConverter"},
		{"CmdScheduler", "Command Scheduler", "CramSim.c_CmdScheduler"},
		{"DeviceDriver", "device driver", "CramSim.c_DeviceDriver"},
		{NULL, NULL, NULL } };

static const char* c_Controller_TxnGenReq_port_events[] = { "c_txnGenReqEvent", NULL };
static const char* c_Controller_TxnGenRes_port_events[] = { "c_txnGenResEvent", NULL };
static const char* c_Controller_DeviceReq_port_events[] = { "c_DeviceReqEvent", NULL };
static const char* c_Controller_DeviceRes_port_events[] = { "c_DeviceResEvent", NULL };
static const char* c_Controller_TxnGenResToken_port_events[] = { "c_txnGenResTokenEvent", NULL };
static const char* c_Controller_TxnGenReqToken_port_events[] = { "c_txnGenReqTokenEvent", NULL };
static const char* c_Controller_DeviceReqToken_port_events[] = { "c_DeviceReqTokenEvent", NULL };

static const ElementInfoPort c_Controller_ports[] = {
		{"inTxnGenReqPtr", "link to controller for incoming req cmds from txn gen", c_Controller_TxnGenReq_port_events},
		{"outTxnGenResPtr", "link to controller for outgoing res cmds to txn gen", c_Controller_TxnGenRes_port_events},
		{"inDeviceResPtr", "link to controller for incoming res cmds from device", c_Controller_DeviceRes_port_events},
		{"outDeviceReqPtr", "link to controller for outgoing req cmds to device", c_Controller_DeviceReq_port_events},
		{"inTxnGenResQTokenChg", "link to controller for incoming res txn tokens",c_Controller_TxnGenResToken_port_events},
		{"outTxnGenReqQTokenChg", "link to controller for outgoing req txn tokens",c_Controller_TxnGenReqToken_port_events},
		{"inDeviceReqQTokenChg", "link to controller for incoming req cmd tokens",c_Controller_DeviceReqToken_port_events},
		{NULL, NULL, NULL}
};

/*----SETUP c_BankReceiver STRUCTURES----*/
static const ElementInfoParam c_BankReceiver_params[] = {
		{NULL, NULL, NULL } };

static const char* c_BankReceiver_cmdReq_port_events[] = { "c_CmdReqEvent", NULL };
static const char* c_BankReceiver_cmdRes_port_events[] = { "c_CmdResEvent", NULL };

static const ElementInfoPort c_BankReceiver_ports[] = {
		{"inDeviceDriverReqPtr", "link to c_BankReceiver for incoming req cmds", c_BankReceiver_cmdReq_port_events},
		{"outDeviceDriverResPtr", "link to c_BankReceiver for outgoing res cmds", c_BankReceiver_cmdRes_port_events},
		{NULL, NULL, NULL}
};

/*----SETUP c_BankReceiver STRUCTURES----*/
static const ElementInfoParam c_Dimm_params[] = {
		{"numRanksPerChannel", "Total number of ranks per channel", NULL},
		{"numBankGroupsPerRank", "Total number of bank groups per rank", NULL},
		{"numBanksPerBankGroup", "Total number of banks per group", NULL},
		{"boolAllocateCmdResACT", "Allocate space in Controller Res Q for ACT Cmds", NULL},
		{"boolAllocateCmdResREAD", "Allocate space in Controller Res Q for READ Cmds", NULL},
		{"boolAllocateCmdResREADA", "Allocate space in Controller Res Q for READA Cmds", NULL},
		{"boolAllocateCmdResWRITE", "Allocate space in Controller Res Q for WRITE Cmds", NULL},
		{"boolAllocateCmdResWRITEA", "Allocate space in Controller Res Q for WRITEA Cmds", NULL},
		{"boolAllocateCmdResPRE", "Allocate space in Controller Res Q for PRE Cmds", NULL},
		{NULL, NULL, NULL } };

static const char* c_Dimm_cmdReq_port_events[] = { "c_CmdReqEvent", NULL };
static const char* c_Dimm_cmdRes_port_events[] = { "c_CmdResEvent", NULL };

static const ElementInfoPort c_Dimm_ports[] = {
		{"inCtrlReqPtr", "link to c_Dimm for incoming req cmds", c_Dimm_cmdReq_port_events},
		{"outCtrlResPtr", "link to c_Dimm for outgoing res cmds", c_Dimm_cmdRes_port_events},
		{NULL, NULL, NULL}
};

static const ElementInfoStatistic c_Dimm_stats[] = {
  {"actCmdsRecvd", "Number of activate commands received", "activates", 1}, // Name, Desc, Units, Enable Level
  {"readCmdsRecvd", "Number of read commands received", "reads", 1}, 
  {"readACmdsRecvd", "Number of read with autoprecharge commands received", "readAs", 1},
  {"writeCmdsRecvd", "Number of write commands received", "writes", 1}, 
  {"writeACmdsRecvd", "Number of write with autoprecharge commands received", "writeAs", 1},
  {"preCmdsRecvd", "Number of precharge commands received", "precharges", 1},
  {"refCmdsRecvd", "Number of refresh commands received", "refreshes", 1},
  {"totalRowHits", "Number of total row hits", "hits", 1},
  // begin autogenerated section
  {"bankACTsRecvd_0", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_0", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_0", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_0", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_0", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_1", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_1", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_1", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_1", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_1", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_2", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_2", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_2", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_2", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_2", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_3", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_3", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_3", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_3", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_3", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_4", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_4", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_4", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_4", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_4", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_5", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_5", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_5", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_5", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_5", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_6", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_6", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_6", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_6", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_6", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_7", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_7", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_7", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_7", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_7", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_8", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_8", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_8", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_8", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_8", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_9", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_9", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_9", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_9", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_9", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_10", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_10", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_10", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_10", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_10", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_11", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_11", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_11", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_11", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_11", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_12", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_12", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_12", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_12", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_12", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_13", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_13", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_13", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_13", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_13", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_14", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_14", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_14", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_14", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_14", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_15", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_15", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_15", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_15", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_15", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_16", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_16", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_16", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_16", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_16", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_17", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_17", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_17", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_17", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_17", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_18", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_18", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_18", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_18", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_18", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_19", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_19", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_19", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_19", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_19", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_20", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_20", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_20", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_20", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_20", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_21", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_21", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_21", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_21", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_21", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_22", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_22", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_22", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_22", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_22", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_23", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_23", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_23", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_23", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_23", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_24", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_24", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_24", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_24", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_24", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_25", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_25", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_25", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_25", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_25", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_26", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_26", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_26", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_26", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_26", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_27", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_27", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_27", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_27", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_27", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_28", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_28", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_28", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_28", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_28", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_29", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_29", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_29", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_29", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_29", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_30", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_30", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_30", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_30", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_30", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_31", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_31", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_31", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_31", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_31", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_32", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_32", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_32", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_32", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_32", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_33", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_33", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_33", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_33", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_33", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_34", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_34", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_34", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_34", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_34", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_35", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_35", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_35", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_35", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_35", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_36", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_36", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_36", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_36", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_36", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_37", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_37", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_37", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_37", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_37", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_38", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_38", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_38", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_38", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_38", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_39", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_39", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_39", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_39", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_39", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_40", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_40", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_40", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_40", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_40", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_41", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_41", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_41", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_41", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_41", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_42", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_42", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_42", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_42", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_42", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_43", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_43", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_43", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_43", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_43", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_44", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_44", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_44", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_44", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_44", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_45", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_45", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_45", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_45", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_45", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_46", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_46", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_46", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_46", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_46", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_47", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_47", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_47", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_47", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_47", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_48", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_48", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_48", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_48", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_48", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_49", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_49", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_49", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_49", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_49", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_50", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_50", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_50", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_50", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_50", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_51", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_51", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_51", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_51", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_51", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_52", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_52", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_52", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_52", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_52", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_53", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_53", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_53", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_53", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_53", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_54", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_54", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_54", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_54", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_54", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_55", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_55", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_55", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_55", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_55", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_56", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_56", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_56", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_56", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_56", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_57", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_57", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_57", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_57", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_57", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_58", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_58", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_58", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_58", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_58", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_59", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_59", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_59", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_59", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_59", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_60", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_60", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_60", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_60", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_60", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_61", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_61", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_61", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_61", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_61", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_62", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_62", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_62", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_62", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_62", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_63", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_63", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_63", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_63", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_63", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_64", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_64", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_64", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_64", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_64", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_65", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_65", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_65", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_65", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_65", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_66", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_66", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_66", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_66", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_66", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_67", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_67", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_67", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_67", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_67", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_68", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_68", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_68", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_68", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_68", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_69", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_69", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_69", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_69", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_69", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_70", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_70", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_70", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_70", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_70", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_71", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_71", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_71", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_71", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_71", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_72", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_72", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_72", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_72", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_72", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_73", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_73", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_73", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_73", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_73", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_74", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_74", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_74", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_74", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_74", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_75", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_75", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_75", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_75", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_75", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_76", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_76", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_76", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_76", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_76", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_77", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_77", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_77", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_77", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_77", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_78", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_78", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_78", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_78", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_78", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_79", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_79", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_79", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_79", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_79", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_80", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_80", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_80", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_80", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_80", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_81", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_81", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_81", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_81", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_81", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_82", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_82", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_82", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_82", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_82", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_83", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_83", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_83", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_83", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_83", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_84", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_84", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_84", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_84", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_84", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_85", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_85", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_85", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_85", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_85", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_86", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_86", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_86", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_86", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_86", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_87", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_87", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_87", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_87", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_87", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_88", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_88", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_88", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_88", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_88", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_89", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_89", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_89", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_89", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_89", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_90", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_90", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_90", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_90", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_90", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_91", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_91", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_91", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_91", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_91", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_92", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_92", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_92", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_92", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_92", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_93", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_93", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_93", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_93", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_93", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_94", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_94", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_94", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_94", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_94", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_95", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_95", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_95", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_95", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_95", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_96", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_96", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_96", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_96", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_96", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_97", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_97", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_97", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_97", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_97", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_98", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_98", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_98", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_98", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_98", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_99", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_99", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_99", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_99", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_99", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_100", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_100", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_100", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_100", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_100", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_101", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_101", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_101", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_101", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_101", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_102", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_102", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_102", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_102", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_102", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_103", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_103", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_103", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_103", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_103", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_104", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_104", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_104", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_104", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_104", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_105", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_105", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_105", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_105", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_105", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_106", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_106", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_106", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_106", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_106", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_107", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_107", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_107", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_107", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_107", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_108", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_108", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_108", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_108", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_108", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_109", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_109", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_109", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_109", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_109", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_110", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_110", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_110", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_110", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_110", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_111", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_111", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_111", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_111", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_111", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_112", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_112", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_112", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_112", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_112", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_113", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_113", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_113", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_113", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_113", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_114", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_114", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_114", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_114", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_114", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_115", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_115", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_115", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_115", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_115", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_116", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_116", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_116", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_116", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_116", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_117", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_117", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_117", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_117", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_117", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_118", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_118", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_118", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_118", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_118", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_119", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_119", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_119", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_119", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_119", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_120", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_120", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_120", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_120", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_120", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_121", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_121", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_121", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_121", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_121", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_122", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_122", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_122", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_122", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_122", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_123", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_123", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_123", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_123", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_123", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_124", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_124", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_124", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_124", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_124", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_125", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_125", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_125", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_125", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_125", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_126", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_126", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_126", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_126", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_126", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_127", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_127", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_127", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_127", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_127", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_128", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_128", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_128", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_128", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_128", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_129", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_129", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_129", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_129", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_129", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_130", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_130", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_130", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_130", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_130", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_131", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_131", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_131", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_131", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_131", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_132", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_132", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_132", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_132", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_132", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_133", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_133", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_133", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_133", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_133", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_134", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_134", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_134", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_134", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_134", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_135", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_135", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_135", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_135", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_135", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_136", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_136", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_136", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_136", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_136", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_137", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_137", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_137", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_137", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_137", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_138", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_138", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_138", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_138", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_138", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_139", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_139", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_139", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_139", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_139", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_140", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_140", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_140", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_140", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_140", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_141", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_141", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_141", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_141", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_141", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_142", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_142", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_142", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_142", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_142", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_143", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_143", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_143", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_143", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_143", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_144", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_144", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_144", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_144", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_144", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_145", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_145", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_145", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_145", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_145", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_146", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_146", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_146", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_146", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_146", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_147", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_147", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_147", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_147", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_147", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_148", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_148", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_148", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_148", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_148", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_149", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_149", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_149", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_149", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_149", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_150", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_150", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_150", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_150", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_150", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_151", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_151", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_151", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_151", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_151", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_152", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_152", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_152", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_152", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_152", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_153", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_153", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_153", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_153", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_153", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_154", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_154", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_154", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_154", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_154", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_155", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_155", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_155", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_155", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_155", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_156", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_156", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_156", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_156", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_156", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_157", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_157", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_157", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_157", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_157", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_158", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_158", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_158", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_158", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_158", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_159", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_159", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_159", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_159", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_159", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_160", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_160", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_160", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_160", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_160", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_161", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_161", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_161", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_161", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_161", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_162", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_162", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_162", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_162", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_162", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_163", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_163", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_163", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_163", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_163", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_164", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_164", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_164", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_164", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_164", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_165", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_165", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_165", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_165", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_165", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_166", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_166", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_166", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_166", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_166", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_167", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_167", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_167", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_167", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_167", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_168", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_168", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_168", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_168", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_168", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_169", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_169", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_169", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_169", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_169", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_170", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_170", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_170", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_170", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_170", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_171", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_171", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_171", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_171", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_171", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_172", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_172", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_172", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_172", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_172", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_173", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_173", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_173", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_173", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_173", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_174", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_174", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_174", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_174", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_174", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_175", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_175", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_175", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_175", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_175", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_176", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_176", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_176", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_176", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_176", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_177", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_177", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_177", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_177", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_177", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_178", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_178", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_178", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_178", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_178", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_179", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_179", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_179", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_179", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_179", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_180", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_180", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_180", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_180", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_180", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_181", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_181", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_181", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_181", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_181", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_182", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_182", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_182", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_182", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_182", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_183", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_183", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_183", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_183", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_183", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_184", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_184", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_184", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_184", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_184", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_185", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_185", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_185", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_185", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_185", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_186", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_186", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_186", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_186", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_186", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_187", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_187", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_187", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_187", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_187", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_188", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_188", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_188", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_188", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_188", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_189", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_189", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_189", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_189", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_189", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_190", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_190", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_190", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_190", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_190", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_191", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_191", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_191", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_191", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_191", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_192", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_192", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_192", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_192", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_192", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_193", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_193", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_193", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_193", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_193", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_194", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_194", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_194", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_194", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_194", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_195", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_195", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_195", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_195", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_195", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_196", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_196", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_196", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_196", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_196", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_197", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_197", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_197", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_197", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_197", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_198", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_198", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_198", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_198", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_198", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_199", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_199", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_199", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_199", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_199", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_200", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_200", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_200", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_200", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_200", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_201", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_201", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_201", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_201", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_201", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_202", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_202", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_202", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_202", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_202", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_203", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_203", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_203", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_203", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_203", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_204", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_204", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_204", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_204", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_204", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_205", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_205", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_205", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_205", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_205", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_206", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_206", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_206", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_206", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_206", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_207", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_207", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_207", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_207", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_207", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_208", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_208", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_208", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_208", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_208", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_209", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_209", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_209", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_209", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_209", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_210", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_210", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_210", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_210", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_210", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_211", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_211", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_211", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_211", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_211", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_212", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_212", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_212", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_212", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_212", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_213", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_213", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_213", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_213", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_213", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_214", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_214", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_214", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_214", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_214", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_215", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_215", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_215", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_215", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_215", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_216", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_216", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_216", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_216", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_216", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_217", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_217", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_217", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_217", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_217", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_218", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_218", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_218", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_218", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_218", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_219", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_219", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_219", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_219", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_219", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_220", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_220", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_220", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_220", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_220", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_221", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_221", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_221", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_221", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_221", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_222", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_222", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_222", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_222", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_222", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_223", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_223", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_223", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_223", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_223", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_224", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_224", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_224", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_224", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_224", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_225", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_225", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_225", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_225", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_225", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_226", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_226", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_226", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_226", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_226", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_227", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_227", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_227", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_227", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_227", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_228", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_228", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_228", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_228", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_228", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_229", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_229", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_229", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_229", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_229", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_230", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_230", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_230", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_230", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_230", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_231", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_231", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_231", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_231", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_231", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_232", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_232", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_232", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_232", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_232", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_233", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_233", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_233", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_233", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_233", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_234", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_234", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_234", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_234", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_234", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_235", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_235", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_235", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_235", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_235", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_236", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_236", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_236", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_236", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_236", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_237", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_237", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_237", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_237", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_237", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_238", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_238", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_238", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_238", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_238", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_239", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_239", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_239", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_239", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_239", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_240", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_240", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_240", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_240", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_240", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_241", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_241", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_241", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_241", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_241", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_242", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_242", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_242", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_242", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_242", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_243", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_243", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_243", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_243", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_243", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_244", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_244", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_244", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_244", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_244", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_245", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_245", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_245", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_245", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_245", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_246", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_246", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_246", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_246", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_246", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_247", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_247", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_247", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_247", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_247", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_248", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_248", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_248", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_248", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_248", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_249", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_249", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_249", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_249", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_249", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_250", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_250", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_250", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_250", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_250", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_251", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_251", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_251", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_251", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_251", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_252", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_252", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_252", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_252", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_252", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_253", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_253", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_253", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_253", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_253", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_254", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_254", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_254", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_254", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_254", "Number of row hits at bank x", "commands", 5},
  {"bankACTsRecvd_255", "Number of activate commands received at bank x", "commands", 5},
  {"bankREADsRecvd_255", "Number of read commands received at bank x", "commands", 5},
  {"bankWRITEsRecvd_255", "Number of write commands received at bank x", "commands", 5},
  {"bankPREsRecvd_255", "Number of write commands received at bank x", "commands", 5},
  {"bankRowHits_255", "Number of row hits at bank x", "commands", 5},
  {NULL, NULL, NULL, 0}
};


static const ElementInfoComponent CramSimComponents[] = {
		{ "c_TxnGen", 							// Name
		"Test Txn Generator",			// Description
		NULL, 										// PrintHelp
		create_c_TxnGen, 						// Allocator
		c_TxnGen_params, 						// Parameters
		c_TxnGen_ports, 							// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		c_TxnGen_stats 										// Statistics
		},{ "c_TxnGenSeq", 							// Name
		"Test Txn Sequential Generator",			// Description
		NULL, 										// PrintHelp
		create_c_TxnGenSeq, 						// Allocator
		c_TxnGenSeq_params, 						// Parameters
		c_TxnGenSeq_ports, 							// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		NULL 										// Statistics
		},
		{ "c_TxnGenRand", 							// Name
		"Test Txn Random Generator",			 	// Description
		NULL, 										// PrintHelp
		create_c_TxnGenRand, 						// Allocator
		c_TxnGenRand_params, 						// Parameters
		c_TxnGenRand_ports, 						// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		c_TxnGenRand_stats								// Statistics
		},
		{ "c_TracefileReader", 							// Name
		"Test Trace file Generator",			 	// Description
		NULL, 										// PrintHelp
		create_c_TracefileReader, 						// Allocator
		c_TracefileReader_params, 						// Parameters
		c_TracefileReader_ports, 						// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		NULL 										// Statistics
		},
		{ "c_TraceReader", 							// Name
		"Test Trace file Generator",			 	// Description
		NULL, 										// PrintHelp
		create_c_TraceReader, 						// Allocator
		c_TraceReader_params, 						// Parameters
		c_TraceReader_ports, 						// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		c_TraceReader_stats							// Statistics
		},
		{ "c_DramSimTraceReader", 							// Name
		"Test DRAMSim2 Trace file Generator",			 	// Description
		NULL, 										// PrintHelp
		create_c_DramSimTraceReader, 						// Allocator
		c_DramSimTraceReader_params, 						// Parameters
		c_DramSimTraceReader_ports, 						// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		c_DramSimTraceReader_stats							// Statistics
		},
		{ "c_USimmTraceReader", 							// Name
		"Test USimm Trace file Generator",			 	// Description
		NULL, 										// PrintHelp
		create_c_USimmTraceReader, 						// Allocator
		c_USimmTraceReader_params, 						// Parameters
		c_USimmTraceReader_ports, 						// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		NULL 										// Statistics
		},
		{ "c_TxnDriver", 							// Name
		"Test Txn Driver",						 	// Description
		NULL, 										// PrintHelp
		create_c_TxnDriver, 						// Allocator
		c_TxnDriver_params, 						// Parameters
		c_TxnDriver_ports, 							// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		NULL 										// Statistics
		},
		{ "c_CmdDriver",	 						// Name
		"Test Cmd Driver",				 			// Description
		NULL, 										// PrintHelp
		create_c_CmdDriver, 						// Allocator
		c_CmdDriver_params, 						// Parameters
		c_CmdDriver_ports, 							// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		NULL 										// Statistics
		},
		{ "c_BankReceiver",	 						// Name
		"Test Bank Receiver",				 		// Description
		NULL, 										// PrintHelp
		create_c_BankReceiver, 						// Allocator
		c_BankReceiver_params, 						// Parameters
		c_BankReceiver_ports, 						// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		NULL 										// Statistics
		},
		{ "c_Dimm",			 						// Name
		"Test DIMM",				 				// Description
		NULL, 										// PrintHelp
		create_c_Dimm, 						// Allocator
		c_Dimm_params, 						// Parameters
		c_Dimm_ports, 						// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		c_Dimm_stats									// Statistics
		},
		{ "c_MemhBridge",			 						// Name
		"Bridge to communicate with MemoryHierarchy",				 				// Description
		NULL, 										// PrintHelp
		create_c_MemhBridge, 						// Allocator
		c_MemhBridge_params, 						// Parameters
		c_MemhBridge_ports, 						// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		c_MemhBridge_stats 										// Statistics
		},
		{ "c_MemhBridgeContent",			 						// Name
		"Bridge to communicate with MemoryHierarchy and store memory content",				 				// Description
		NULL, 										// PrintHelp
		create_c_MemhBridgeContent, 						// Allocator
		c_MemhBridge_params, 						// Parameters
		c_MemhBridgeContent_ports, 						// Ports
		COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
		c_MemhBridge_stats 										// Statistics
		},
		{ "c_Controller",			 						// Name
			"Memory Controller",				 				// Description
			NULL, 										// PrintHelp
					create_c_Controller, 						// Allocator
					c_Controller_params, 						// Parameters
					c_Controller_ports, 						// Ports
					COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
					NULL 										// Statistics
		},
		{ "c_TxnDispatcher",			 						// Name
			"Transaction dispatcher",				 				// Description
			NULL, 										// PrintHelp
					create_c_TxnDispatcher, 						// Allocator
					c_TxnDispatcher_params, 						// Parameters
					c_TxnDispatcher_ports, 						// Ports
					COMPONENT_CATEGORY_UNCATEGORIZED, 			// Category
					NULL 										// Statistics
		},
		{ NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL } };


static const ElementInfoSubComponent CramSimSubComponents[] = {
		{ "c_AddressHasher", 							// Name
		  "Hashes addresses based on config parameters",			// Description
		  NULL, 										// PrintHelp
		  create_c_AddressHasher, 						// Allocator
		  c_AddressHasher_params, 						// Parameters
		  NULL,										//Interface
		  "SST::CramSim::Controller::AddressHasher" 			// Category
		},
		{ "c_TxnScheduler",	 							// Name
		"Transaction Scheduler",				 			// Description
		NULL, 										// PrintHelp
		create_c_TxnScheduler, 							// Allocator
		c_TxnScheduler_params, 							// Parameters
		c_TxnScheduler_stats,
				"SST::CramSim::Controller::TxnScheduler" 			// Category
		},
		{ "c_TxnConverter",	 							// Name
		"Transaction Converter",				 			// Description
		NULL, 										// PrintHelp
		create_c_TxnConverter, 							// Allocator
		c_TxnConverter_params, 							// Parameters
		c_TxnConverter_stats,
				"SST::CramSim::Controller::TxnConverter" 			// Category
		},
		{"c_DeviceDriver",                                // Name
			"Dram Control Unit",                            // Description
			NULL,                                        // PrintHelp
			create_c_DeviceDriver,                            // Allocator
			c_DeviceDriver_params,                            // Parameters
			c_DeviceDriver_stats,
				"SST::CramSim::Controller::DeviceDriver"            // Category
		},
		{"c_CmdScheduler",                                // Name
				"Command Scheduler",                            // Description
				NULL,                                        // PrintHelp
				create_c_CmdScheduler,                            // Allocator
				c_CmdScheduler_params,                            // Parameters
				c_CmdScheduler_stats,
				"SST::CramSim::Controller::CmdScheduler"            // Category
		},
		{ NULL, NULL, NULL, NULL, NULL, NULL}
};


extern "C" {
ElementLibraryInfo CramSim_eli = { "CramSim", // Name
		"Library with transaction generation components", // Description
		CramSimComponents, // Components
		NULL, // Events
		NULL, // Introspectors
		NULL, // Modules
		CramSimSubComponents, // Subcomponents
		NULL, // Partitioners
		NULL, // Python Module Generator
		NULL // Generators
		};
}
