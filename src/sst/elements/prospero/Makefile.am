
AM_CPPFLAGS = \
        $(MPI_CPPFLAGS) \
        -DPROSPERO_TOOL_DIR="$(libexecdir)"

compdir = $(pkglibdir)
comp_LTLIBRARIES = libprospero.la

libprospero_la_SOURCES = \
	libprospero.cc \
        proscpu.h \
        proscpu.cc \
	prosreader.h \
	prostextreader.h \
	prostextreader.cc \
	prosbinaryreader.h \
	prosbinaryreader.cc \
	prosmemmgr.h \
	prosmemmgr.cc \
  atomichandler.h

EXTRA_DIST = \
        tests/array/trace-binary.py \
        tests/array/trace-binary-withdramsim.py \
        tests/array/trace-compressed.py \
        tests/array/trace-compressed-withdramsim.py \
        tests/array/trace-text.py \
        tests/array/trace-text-withdramsim.py \
        tests/array/trace-common.py \
        tests/array/array.c \
        tests/array/Makefile \
        tracetool/Makefile \
        tracetool/Makefile.osx \
        tracetool/sstmemtrace.cc \
        tracetool/indigo.cc \
        tracetool/api/Makefile \
        tracetool/api/Makefile.osx \
        tracetool/api/prospero.c \
        tracetool/api/prospero.h

libprospero_la_LDFLAGS = -module -avoid-version
libprospero_la_LIBADD = $(SHM_LIB)

if USE_LIBZ
libprospero_la_LIBADD += -lz

libprospero_la_SOURCES += \
	prosbingzreader.h \
	prosbingzreader.cc
endif

if HAVE_PINTOOL

bin_PROGRAMS = sst-prospero-trace
sst_prospero_trace_SOURCES = runprosperotrace.cc
AM_CPPFLAGS +=  $(PINTOOL_CPPFLAGS)

if SST_COMPILE_OSX

all-local: tracetool/sstmemtrace.cc
	$(CXX) -O3 -shared -std=c++11 \
	$(CPPFLAGS) \
	$(LIBZ_CPPFLAGS) \
	-DBIGARRAY_MULTIPLIER=1 \
	-DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_MAC \
	-I$(PINTOOL_DIR)/source/include/pin \
	-I$(PINTOOL_DIR)/ \
	-I$(PINTOOL_DIR)/extras/components/include \
	-I$(PINTOOL_DIR)/source/include/pin/gen/ \
	-I$(PINTOOL_DIR)/extras/xed-intel64/include \
	-I$(top_srcdir)/sst \
	-fomit-frame-pointer -fno-stack-protector \
	-Wl,-exported_symbols_list \
	-Wl,$(PINTOOL_DIR)/source/include/pin/pintool.exp \
	-L$(PINTOOL_DIR)/intel64/lib \
	-L$(PINTOOL_DIR)/intel64/lib-ext \
	-L$(PINTOOL_DIR)/extras/xed-intel64/lib \
	-o prosperotrace.dylib $(top_srcdir)/src/sst/elements/prospero/tracetool/sstmemtrace.cc \
	-stdlib=libstdc++ \
	-lpin -lxed -lpindwarf -lpthread $(LIBZ_LDFLAGS) $(LIBZ_LIB)

install-exec-hook: prosperotrace.dylib
	cp prosperotrace.dylib $(libexecdir)/prosperotrace.dylib

else

#all-local:
#       $(CXX) -shared -Wl,--hash-style=sysv -Wl,-Bsymbolic \
#       	-fPIC -O3 \
#       	-fabi-version=2 \
#       	-fomit-frame-pointer \
#       	-fpermissive \
#       	$(CXXFLAGS) \
#       	$(CPPFLAGS) \
#       	$(BOOST_CPPFLAGS) \
#       	$(LIBZ_CPPFLAGS) \
#       	-DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX \
#       	-DPIN_CRT=1 -D__PIN__=1 \
#       	-I./ \
#       	-I$(PINTOOL_DIR)/ \
#       	-I$(PINTOOL_DIR)/source/include/pin \
#       	-I$(PINTOOL_DIR)/source/include/pin/gen \
#       	-I$(PINTOOL_DIR)/extras/stlport/include \
#       	-I$(PINTOOL_DIR)/extras/libstdc++/include \
#       	-I$(PINTOOL_DIR)/extras/crt/include \
#       	-I$(PINTOOL_DIR)/extras/crt/include/arch-x86_64 \
#       	-I$(PINTOOL_DIR)/extras/crt/include/kernel/uapi \
#       	-I$(PINTOOL_DIR)/extras/crt/include/kernel/uapi/asm-x86 \
#       	-I$(PINTOOL_DIR)/extras/components/include \
#       	-I$(PINTOOL_DIR)/extras/xed-intel64/include \
#       	-I$(PINTOOL_DIR)/extras/xed-intel64/include/xed \
#       	-I$(PINTOOL_DIR)/source/tools/InstLib \
#       	-I$(top_srcdir)/src/sst \
#       	-I$(top_srcdir)/src/sst/elements/ariel \
#       	-I$(top_srcdir)/src \
#       	-Wl,--version-script=$(PINTOOL_DIR)/source/include/pin/pintool.ver \
#       	-L$(PINTOOL_DIR)/intel64/runtime/pincrt \
#       	-L$(PINTOOL_DIR)/intel64/lib \
#       	-L$(PINTOOL_DIR)/intel64/lib-ext \
#       	-L$(PINTOOL_DIR)/extras/xed-intel64/lib \
#       	-L$(PINTOOL_DIR)/intel64/runtime/glibc \
#       	-o prosperotrace.so $(top_srcdir)/src/sst/elements/prospero/tracetool/sstmemtrace.cc \
#       	-lpin -lxed -lpin3dwarf $(LIBZ_LDFLAGS) \
#       	-lstlport-dynamic -lm-dynamic -lc-dynamic -lunwind-dynamic -ldl-dynamic \
#       	-Wl,-Bstatic -lrt -Wl,-Bdynamic
#

#all-local: tracetool/sstmemtrace.cc
#       $(CXX) -shared -Wl,--hash-style=sysv -Wl,-Bsymbolic \
#       -fPIC -O3 \
#       -fomit-frame-pointer \
#       $(CPPFLAGS) \
#       $(LIBZ_CPPFLAGS) \
#       -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX \
#       -I$(PINTOOL_DIR)/source/include/pin \
#       -I$(PINTOOL_DIR)/ \
#       -I$(PINTOOL_DIR)/extras/components/include \
#       -I$(PINTOOL_DIR)/source/include/pin/gen/ \
#       -I$(PINTOOL_DIR)/extras/xed-intel64/include \
#       -I$(top_srcdir)/sst \
#       -Wl,--version-script=$(PINTOOL_DIR)/source/include/pin/pintool.ver \
#       -L$(PINTOOL_DIR)/intel64/lib \
#       -L$(PINTOOL_DIR)/intel64/lib-ext \
#       -L$(PINTOOL_DIR)/intel64/runtime/glibc \
#       -L$(PINTOOL_DIR)/extras/xed-intel64/lib \
#       -o prosperotrace.so $(top_srcdir)/src/sst/elements/prospero/tracetool/sstmemtrace.cc \
#       -ldl -lpin -lxed -lpindwarf -ldl -lpthread -lrt $(LIBZ_LDFLAGS) $(LIBZ_LIB)
#
all-local: tracetool/indigotrace.cc
	$(CXX) -shared -Wl,--hash-style=sysv -Wl,-Bsymbolic \
	-fPIC -O3 -std=c++11 \
	-fomit-frame-pointer \
	$(CPPFLAGS) \
	$(LIBZ_CPPFLAGS) \
	-DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX \
	-I$(PINTOOL_DIR)/source/include/pin \
	-I$(PINTOOL_DIR)/ \
	-I$(PINTOOL_DIR)/extras/components/include \
	-I$(PINTOOL_DIR)/source/include/pin/gen/ \
	-I$(PINTOOL_DIR)/extras/xed-intel64/include \
	-I$(top_srcdir)/sst \
	-Wl,--version-script=$(PINTOOL_DIR)/source/include/pin/pintool.ver \
	-L$(PINTOOL_DIR)/intel64/lib \
	-L$(PINTOOL_DIR)/intel64/lib-ext \
	-L$(PINTOOL_DIR)/intel64/runtime/glibc \
	-L$(PINTOOL_DIR)/extras/xed-intel64/lib \
	-o prosperotrace.so $(top_srcdir)/src/sst/elements/prospero/tracetool/indigotrace.cc \
	-ldl -lpin -lxed -lpindwarf -ldl -lpthread -lrt $(LIBZ_LDFLAGS) $(LIBZ_LIB)


install-exec-hook: prosperotrace.so
	cp prosperotrace.so $(libexecdir)/prosperotrace.so

endif
endif
