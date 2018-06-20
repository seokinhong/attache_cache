# -*- Makefile -*-
#
#

AM_CPPFLAGS = \
	$(MPI_CPPFLAGS)

compdir = $(pkglibdir)
comp_LTLIBRARIES = libCramSim.la
libCramSim_la_SOURCES = \
	CramSim.cpp \
	c_TraceFileReader.hpp \
	c_TraceFileReader.cpp \
	c_Dimm.hpp \
	c_Dimm.cpp \
	c_Bank.hpp \
	c_Bank.cpp \
	c_TxnConverter.hpp \
	c_TxnConverter.cpp \
	c_Transaction.hpp \
	c_Transaction.cpp \
	c_AddressHasher.hpp \
	c_AddressHasher.cpp \
	c_HashedAddress.hpp \
	c_HashedAddress.cpp \
	c_DeviceDriver.hpp \
	c_DeviceDriver.cpp \
	c_Controller.cpp \
	c_Controller.hpp \
	c_BankCommand.hpp \
	c_BankCommand.cpp \
	c_BankState.hpp \
	c_BankStateActivating.hpp \
	c_BankStateActivating.cpp \
	c_BankStateActive.hpp \
	c_BankStateActive.cpp \
	c_BankStateIdle.hpp \
	c_BankStateIdle.cpp \
	c_BankStatePrecharge.hpp \
	c_BankStatePrecharge.cpp \
	c_BankStateRead.hpp \
	c_BankStateRead.cpp \
	c_BankStateReadA.hpp \
	c_BankStateReadA.cpp \
	c_BankStateRefresh.hpp \
	c_BankStateRefresh.cpp \
	c_BankStateWrite.hpp \
	c_BankStateWrite.cpp \
	c_BankStateWriteA.hpp \
	c_BankStateWriteA.cpp \
	c_Rank.hpp \
	c_Rank.cpp \
	c_Channel.hpp \
	c_Channel.cpp \
	c_BankGroup.hpp \
	c_BankGroup.cpp \
	c_BankInfo.hpp \
	c_BankInfo.cpp \
	c_TxnReqEvent.hpp \
	c_TxnResEvent.hpp \
	c_CmdPtrPkgEvent.hpp \
	c_TokenChgEvent.hpp \
	c_CmdReqEvent.hpp \
	c_CmdResEvent.hpp \
	c_MemhBridge.hpp \
	c_MemhBridge.cpp \
	c_TxnScheduler.cpp \
	c_TxnScheduler.hpp \
	c_CmdScheduler.cpp \
	c_CmdScheduler.hpp \
	c_TxnDispatcher.hpp \
	c_TxnDispatcher.cpp \
	c_TxnGen.hpp \
	c_TxnGen.cpp \
	memReqEvent.hpp \
    c_MemhBridgeContent.cpp \
    c_MemhBridgeContent.hpp \
    c_CompressEngine.cpp \
    c_CompressEngine.hpp \
    c_ControllerPCA.cpp \
    c_ControllerPCA.hpp \
    c_Cache.cpp \
    c_Cache.hpp \
    c_PageAllocator.cpp \
    c_PageAllocator.hpp \
    Types.hpp \
    Cache.cpp \
    Cache.hpp


EXTRA_DIST = \
	README \
	ddr3.cfg \
	ddr4.cfg \
	ddr4_2400.cfg \
	ddr4_2667.cfg \
	ddr4_3200.cfg \
	ddr4_verimem.cfg \
	ddr4_verimem_openbank.cfg \
	hbm_legacy_4h.cfg \
	hbm_legacy_4h_sbr.cfg \
	hbm_pseudo_4h.cfg \
	test_system.cfg \
	test_device.cfg \
	ddr3_power.cfg \
	tests/VeriMem/test_verimem1.py \
	tests/test_txngen.py \
	tests/test_txntrace.py \
	Profiler/memProfiler.cc

libCramSim_la_LDFLAGS = -module -avoid-version

if SST_COMPILE_OSX

all-local: Profiler/memProfiler.cc

	$(CXX) -O3 -shared \
	$(CXXFLAGS) \
	$(CPPFLAGS) \
	$(LIBZ_CPPFLAGS) \
	-DBIGARRAY_MULTIPLIER=1 \
	-DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_MAC \
	-I./ \
	-I$(PINTOOL_DIR)/source/include/pin \
	-I$(PINTOOL_DIR)/ \
	-I$(PINTOOL_DIR)/extras/components/include \
	-I$(PINTOOL_DIR)/source/include/pin/gen/ \
	-I$(PINTOOL_DIR)/extras/xed-intel64/include \
	-I$(top_srcdir)/src/sst \
	-I$(top_srcdir)/src/sst/elements/ariel \
	-fomit-frame-pointer -fno-stack-protector \
	-Wl,-exported_symbols_list \
	-Wl,$(PINTOOL_DIR)/source/include/pin/pintool.exp \
	-L$(PINTOOL_DIR)/intel64/lib \
	-L$(PINTOOL_DIR)/intel64/lib-ext \
	-L$(PINTOOL_DIR)/extras/xed-intel64/lib \
	-o memProfiler.dylib $(top_srcdir)/src/sst/elements/CramSim/Profiler/memProfiler.cc \
	-stdlib=libstdc++ \
	-lpin -lxed -lpindwarf -lpthread $(LIBZ_LDFLAGS) $(LIBZ_LIB)

install-exec-hook:
	$(MKDIR_P) $(libexecdir)
	$(INSTALL) memProfiler.dylib $(libexecdir)/memProfiler.dylib

else
all-local:
	$(CXX) -shared -Wl,--hash-style=sysv -Wl,-Bsymbolic \
	-fPIC -O3 \
	-fomit-frame-pointer \
	$(CXXFLAGS) \
	$(CPPFLAGS) \
	$(LIBZ_CPPFLAGS) \
	-DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX \
	-I./ \
	-I$(PINTOOL_DIR)/source/include/pin \
	-I$(PINTOOL_DIR)/ \
	-I$(PINTOOL_DIR)/extras/components/include \
	-I$(PINTOOL_DIR)/source/include/pin/gen/ \
	-I$(PINTOOL_DIR)/extras/xed-intel64/include \
	-I$(top_srcdir)/src/sst \
	-I$(top_srcdir)/src/sst/elements/ariel \
	-Wl,--version-script=$(PINTOOL_DIR)/source/include/pin/pintool.ver \
	-L$(PINTOOL_DIR)/intel64/lib \
	-L$(PINTOOL_DIR)/intel64/lib-ext \
	-L$(PINTOOL_DIR)/intel64/runtime/glibc \
	-L$(PINTOOL_DIR)/extras/xed-intel64/lib \
	-o memProfiler.so $(top_srcdir)/src/sst/elements/CramSim/Profiler/memProfiler.cc \
	-ldl -lpin -lxed -lpindwarf -ldl -lpthread -lrt $(LIBZ_LDFLAGS) $(LIBZ_LIB)


install-exec-hook:
	$(MKDIR_P) $(libexecdir)
	$(INSTALL) memProfiler.so $(libexecdir)/memProfiler.so

endif
