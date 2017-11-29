import sst
import sys
import time
import os

spec_arg = {
    "GemsFDTD":"",
    "omnetpp":"./omnetpp.ini",
	"soplex": "-s1 -e -m45000 ./pds-50.mps > soplex.ref.pds-50.out 2> soplex.ref.pds-50.err",
	"mcf": "./inp.in",
	"lbm": "3000 reference.dat 0 0 ./100_100_130_ldc.of",
	"libquantum": "1397 8",
        "milc": "< ./su3imp.in",
        "leslie3d": "< ./leslie3d.in",
        "sphinx3": "./ctlfile . args.an4",
	"astar": "./BigLakes2048.cfg",
        "wrf": "",
        "h264ref": "-d foreman_ref_encoder_baseline.cfg",
        "sjeng": "ref.txt",
	"cactusADM": "./benchADM.par",
	"zeusmp": "",
        "bwaves": "",
        "gcc": "166.i -o 166.s",
        "perlbench": "-I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1",
        "bzip2": "chicken.jpg 30",
        "gamess": "< cytosine.2.config",
        "gromacs": "-silent -deffnm gromacs -nice 0",
        "namd": "--input namd.input --iterations 38 --output namd.out",
        "gobmk": "--quiet --mode gtp < 13x13.tst",
        "dealII": "23",
        "povray": "SPEC-benchmark-ref.ini",
        "calculix": "-i hyperviscoplastic",
        "hmmer": "nph3.hmm swiss41",
        "xalancbmk" : "-v t5.xml xalanc.xsl"
        }


#######################################################################################################
benchmark=""
maxinst=""
statname=""
warmup=""

def read_arguments():
        global benchmark
        global maxinst
        global statname
        global warmup

	config_file = list()
        override_list = list()
        boolDefaultConfig = True;
        
	for arg in sys.argv:
            if arg.find("--configfile=") != -1:
		substrIndex = arg.find("=")+1
		config_file = arg[substrIndex:]
		print "Config file:", config_file
		boolDefaultConfig = False;
            elif arg.find("--executable=") !=-1:
                substrIndex = arg.find("=")+1
                benchmark = arg[substrIndex:]
                print "executable:", benchmark
            elif arg.find("--maxinst=") !=-1:
                substrIndex = arg.find("=")+1
                maxinst = arg[substrIndex:]
                print "maxinst:", maxinst
            elif arg.find("--warmup=") !=-1:
                substrIndex = arg.find("=")+1
                warmup = arg[substrIndex:]
                print "warmup_inst:", warmup

            elif arg.find("--statname=") !=-1:
                substrIndex = arg.find("=")+1
                statname = arg[substrIndex:]
                print "statname:", statname
  	    elif arg != sys.argv[0]:
                if arg.find("=") == -1:
                    print "Malformed config override found!: ", arg
                    exit(-1)
                override_list.append(arg)
                print "Override: ", override_list[-1]

	
	if boolDefaultConfig == True:
		config_file = "../ddr4_verimem.cfg"
		print "config file is not specified.. using ddr4_verimem.cfg"

	return [config_file, override_list]

def setup_config_params(config_file, override_list):
    l_params = {}
    l_configFile = open(config_file, 'r')
    for l_line in l_configFile:
        l_tokens = l_line.split()
         #print l_tokens[0], ": ", l_tokens[1]
        l_params[l_tokens[0]] = l_tokens[1]

    for override in override_list:
        l_tokens = override.split("=")
        print "Override cfg", l_tokens[0], l_tokens[1]
        l_params[l_tokens[0]] = l_tokens[1]
     
    return l_params

#######################################################################################################

# Command line arguments
g_config_file = "/dccstor/memsim1/pca/config/ddr4-3200.cfg"
g_overrided_list = ""

# Setup global parameters
#[g_boolUseDefaultConfig, g_config_file] = read_arguments()
[g_config_file, g_overrided_list] = read_arguments()
g_params = setup_config_params(g_config_file, g_overrided_list)

# Define SST core options
sst.setProgramOption("timebase", "1ps")
#sst.setProgramOption("stopAtCycle", "21000us")


## Flags
memDebug = 0
memDebugLevel = 0
verbose = 0
controller_verbose = 0
coherenceProtocol = "MESI"
rplPolicy = "lru"
busLat = "50 ps"
#cacheFrequency = "2 Ghz"

#cpuFrequency = "100 Mhz"
#cacheFrequency = "100 Mhz"
#memoryFrequency = "10 Mhz"
cpuFrequency = "4 Ghz"
cacheFrequency = "4 Ghz"
memoryFrequency = "1 Ghz"

g_params["strControllerClockFreq"]= memoryFrequency
defaultLevel = 0
cacheLineSize = 64

corecount = int(g_params["corecount"])
multiprog = 1
memory_size = int(g_params["memsize"])    #GB
metadata_predictor=g_params["metadata_predictor"]
pagesize=4096       #B
pagecount = memory_size*1024*1024*1024/corecount/pagesize
print pagecount
compression_en = g_params["compression_en"]
memcontent_link_en =1
pca_enable = g_params["pca_en"]
oracle_mode =0
metacache_entries=int(g_params["metacache_entries"])*1024

## Application Info
os.environ['SIM_DESC'] = 'EIGHT_CORES'
os.environ['OMP_NUM_THREADS'] = str(corecount)

sst_root = os.getenv( "SST_ROOT" )

## MemHierarchy 
membus = sst.Component("membus", "memHierarchy.Bus")
membus.addParams({
   "bus_frequency" : cacheFrequency,
})

memory = sst.Component("memory", "memHierarchy.MemController")
memory.addParams({
   "do_not_back"           : "1",
   "range_start"           : "0",
   "coherence_protocol"    : coherenceProtocol,
   "debug"                 : memDebug,
   "clock"                 : memoryFrequency,
  "backend" : "memHierarchy.cramsim",
  "backend.access_time" : "1 ns",   # Phy latency
  "backend.mem_size" : "%dGiB"%memory_size,
  "backend.max_outstanding_requests" : 1024,
    "backend.verbose" : 0,
   "request_width"         : cacheLineSize
})

l3 = sst.Component("L3cache", "memHierarchy.Cache")
l3.addParams({
   "cache_frequency"       : cacheFrequency,
   "cache_size"            : "%d MB" % corecount*1,
   "cache_line_size"       : cacheLineSize,
   "associativity"         : "8",
   "access_latency_cycles" : "20",
   "coherence_protocol"    : coherenceProtocol,
   "replacement_policy"    : rplPolicy,
   "L1"                    : "0",
   "debug"                 : memDebug,  
   "debug_level"           : memDebugLevel, 
   "mshr_num_entries"      : "16",
})

# Bus to L3 and L3 <-> MM
BusL3Link = sst.Link("bus_L3")
BusL3Link.connect((membus, "low_network_0", busLat), (l3, "high_network_0", busLat))
L3MemCtrlLink = sst.Link("L3MemCtrl")
L3MemCtrlLink.connect((l3, "low_network_0", busLat), (memory, "direct_link", busLat))

# txn gen --> memHierarchy Bridge
comp_memhBridge = sst.Component("memh_bridge", "CramSim.c_MemhBridge")
comp_memhBridge.addParams(g_params);
comp_memhBridge.addParams({
                    "verbose" : verbose,
                    "numTxnPerCycle" : g_params["numChannels"],
                    })




# controller
comp_controller0 = sst.Component("MemController0", "CramSim.c_ControllerPCA")
comp_controller0.addParams(g_params)
comp_controller0.addParams({
                    "boolPrintCmdTrace" : 0,
                    "verbose" : controller_verbose,
                    "compression_en" : compression_en,
                    "loopback_en" : 0,
                    "pca_enable"  : pca_enable,
                    "oracle_mode" : oracle_mode,
                    "metadata_predictor" : metadata_predictor,   #0: perfect predictor, 1:metacache, 2:2lv
                    "metaCache_entries" : metacache_entries,
                    "TxnConverter" : "CramSim.c_TxnConverter",
                    "AddrMapper" : "CramSim.c_AddressHasher",
                    "CmdScheduler" : "CramSim.c_CmdScheduler" ,
                    "DeviceController" : "CramSim.c_DeviceController",
                    "backing_size" : memory_size*1024*1024*1024
                    })

# memory device
comp_dimm0 = sst.Component("Dimm0", "CramSim.c_Dimm")
comp_dimm0.addParams(g_params)
comp_dimm0.addParams({
        "pca_enable" : pca_enable}
        )



link_dir_cramsim_link = sst.Link("link_dir_cramsim_link")
link_dir_cramsim_link.connect( (memory, "cube_link", "2ns"), (comp_memhBridge, "cpuLink", "2ns") )

# memhBridge(=TxnGen) <-> Memory Controller 
memHLink = sst.Link("memHLink_1")
memHLink.connect( (comp_memhBridge, "memLink", g_params["clockCycle"]), (comp_controller0, "txngenLink", g_params["clockCycle"]) )

# Controller <-> Dimm
cmdLink = sst.Link("cmdLink_1")
cmdLink.connect( (comp_controller0, "memLink", g_params["clockCycle"]), (comp_dimm0, "ctrlLink", g_params["clockCycle"]) )



#memMemContentLink = sst.Link("memContent_link")
#memMemContentLink.connect((comp_controller0, "contentLink", g_params["clockCycle"]), (ariel, "linkMemContent",g_params["clockCycle"]));


comp_controller0.enableAllStatistics()
comp_memhBridge.enableAllStatistics()
comp_dimm0.enableAllStatistics()
l3.enableAllStatistics()



##### CPU ####
for core in range (corecount):
    ariel = sst.Component("A%d"%core, "ariel.ariel")
    ## ariel.addParams(AppArgs)
    ariel.addParams({
       "clock"               :cpuFrequency,
       "verbose"             : verbose,
       "maxcorequeue"        : "512",
       "maxtranscore"        : "64",
       "maxissuepercycle"    : "4",
       "warmup_insts"        : warmup,
       "max_insts"           : maxinst,
       "pipetimeout"         : "0",
       "arielmode"           : "1",
       "executable"          : "./"+benchmark,
       "memorylevels"        : "1",
       "arielinterceptcalls" : "1",
       "corecount"           : "1",
       "defaultlevel"        : defaultLevel,
       "multiprogsim_en"     : multiprog,
       "memcontent_link_en"  : memcontent_link_en,
       "cpuid"               : core,
       "memmgr.pagecount0"   : pagecount,
       "memmgr.pagesize0"    : pagesize,
       "memmgr.pagemappolicy" : "randomized",
       "memmgr.page_populate_0" : "",
       "apparg"              : spec_arg[benchmark]
    })


    i=0;
    print "benchmark" + benchmark
    print "maxinst" + maxinst
    bench_args_=spec_arg[benchmark]
    bench_args=bench_args_.split(" ")
    while i<len(bench_args):
       param_name="apparg%d" % i
       print param_name
       print bench_args[i]
       ariel.addParams({param_name: bench_args[i]})
       i+=1

    ariel.addParams({"appargcount": len(bench_args)})

    ariel.enableAllStatistics();

    l1 = sst.Component("l1cache_%d"%core, "memHierarchy.Cache")
    l1.addParams({
       "cache_frequency"       : cacheFrequency,
       "cache_size"            : "32KB",
       "cache_line_size"       : cacheLineSize,
       "associativity"         : "8",
       "access_latency_cycles" : "4",
       "coherence_protocol"    : coherenceProtocol,
       "replacement_policy"    : rplPolicy,
       "L1"                    : "1",
       "debug"                 : memDebug,  
       "debug_level"           : memDebugLevel, 
    })

    l2 = sst.Component("l2cache_%d"%core, "memHierarchy.Cache")
    l2.addParams({
       "cache_frequency"       : cacheFrequency,
       "cache_size"            : "256 KB",
       "cache_line_size"       : cacheLineSize,
       "associativity"         : "8",
       "access_latency_cycles" : "10",
       "coherence_protocol"    : coherenceProtocol,
       "replacement_policy"    : rplPolicy,
       "L1"                    : "0",
       "debug"                 : memDebug,  
       "debug_level"           : memDebugLevel, 
       "mshr_num_entries"      : "16",
    })
    l1.enableAllStatistics()
    l2.enableAllStatistics()
   ## SST Links
   # Ariel -> L1(PRIVATE) -> L2(PRIVATE)  -> L3 (SHARED) -> DRAM 
    ArielL1Link = sst.Link("cpu_cache_%d"%core)
    ArielL1Link.connect((ariel, "cache_link_0", busLat), (l1, "high_network_0", busLat))
    L1L2Link = sst.Link("l1_l2_%d"%core)
    L1L2Link.connect((l1, "low_network_0", busLat), (l2, "high_network_0", busLat))
    L2MembusLink = sst.Link("l2_membus_%d"%core)
    L2MembusLink.connect((l2, "low_network_0", busLat), (membus, "high_network_%d"%core, busLat))

    MemContentLink = sst.Link("memContent_%d"%core)
    MemContentLink.connect((ariel, "linkMemContent",busLat), (comp_controller0, "lane_%d"%core, busLat));
  #  MemContentLink.connect((ariel, "linkMemContent",busLat), (memcontentbus, "high_network_%d"%core, busLat));
#   MemContentLink.connect((comp_controller0, "contentLink", g_params["clockCycle"]), (ariel, "linkMemContent",g_params["clockCycle"]));
