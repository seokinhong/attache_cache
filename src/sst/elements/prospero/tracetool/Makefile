#!/bin/bash

PINDIR=${INTEL_PIN_DIRECTORY}
CXX=g++
CXXFLAGS = -O3 -std=c++11  -std=c++1y -D__STDC_FORMAT_MACROS 
PREPROC=-DBIGARRAY_MULTIPLIER=1 -DUSING_XED -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX -DPROSPERO_LIBZ
LD=g++
CC=gcc

all: sstmemtrace.so indigo.so

sstmemtrace.so: sstmemtrace.o
	$(LD) -Wl,--hash-style=sysv -shared -Wl,-Bsymbolic \
		-Wl,--version-script=$(PINDIR)/source/include/pin/pintool.ver \
		-L$(PINDIR)/extras/xed-intel64/lib \
		-L$(PINDIR)/intel64/lib \
		-L$(PINDIR)/intel64/lib-ext \
		-o $@ $< \
		-L$(PINDIR)/extras/xed-intel64/lib \
		-L$(PINDIR)/intel64/lib \
		-L$(PINDIR)/intel64/lib-ext -lpin -lxed \
		 -ldl -lz -lpindwarf \

sstmemtrace.o: sstmemtrace.cc
	$(CXX) -g -c -O3 -std=c++11 -fomit-frame-pointer \
        -Wl,-Bsymbolic \
        -Wl,--version-script=$(PINDIR)/source/include/pintool.ver \
	-I $(PINDIR)/source/include/pin \
	-I $(PINDIR)/source/include/pin/gen \
	-I $(PINDIR)/extras/xed-intel64/include \
	-I $(PINDIR)/extras/components/include \
	$(PREPROC) \
	-o $@ \
	$<

indigo.so: indigo.o
	$(LD) -Wl,--hash-style=sysv -shared -Wl,-Bsymbolic \
		-Wl,--version-script=$(PINDIR)/source/include/pin/pintool.ver \
		-L$(PINDIR)/extras/xed-intel64/lib \
		-L$(PINDIR)/intel64/lib \
		-L$(PINDIR)/intel64/lib-ext \
		-o $@ $< \
		-L$(PINDIR)/extras/xed-intel64/lib \
		-L$(PINDIR)/intel64/lib \
		-L$(PINDIR)/intel64/lib-ext -lpin -lxed \
		 -ldl -lz -lpindwarf \

indigo.o: indigo.cc
	$(CXX) -g -c -O3 -std=c++11 -fomit-frame-pointer \
        -Wl,-Bsymbolic \
        -Wl,--version-script=$(PINDIR)/source/include/pintool.ver \
	-I $(PINDIR)/source/include/pin \
	-I $(PINDIR)/source/include/pin/gen \
	-I $(PINDIR)/extras/xed-intel64/include \
	-I $(PINDIR)/extras/components/include \
	$(PREPROC) \
	-o $@ \
	$<


clean:
	rm -f *.o sstmemtrace.so sstmemtrace.dylib indigo.so indigo.dylib
