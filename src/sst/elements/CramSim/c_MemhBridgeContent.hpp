// Copyright 2009-2016 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2016, Sandia Corporation
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef _MEMHBRIDGECONTENT_H
#define _MEMHBRIDGECONTENT_H

#include <stdint.h>
#include <queue>
#include <iostream>
#include <fstream>

//SST includes
#include <sst/core/component.h>
#include <sst/core/link.h>
#include <sst/core/output.h>
#include <sst/elements/memHierarchy/memEvent.h>
#include <sst/elements/memHierarchy/membackend/backing.h>

//local includes
#include "c_Transaction.hpp"
#include "c_MemhBridge.hpp"
#include "c_TxnGen.hpp"
#include "c_CompressEngine.hpp"



namespace SST {
    namespace n_Bank {

        class Backing {
        public:
            Backing(const char* memoryFile, size_t size, size_t offset = 0 ) :m_fd(-1), m_size(size), m_offset(offset) {
                int flags = MAP_PRIVATE;
                if ( memoryFile != NULL) {
                    m_fd = open(memoryFile, O_RDWR);
                    if ( m_fd < 0) {
                        throw 1;
                    }
                } else {
                    flags  |= MAP_ANON;
                }
                m_buffer = (uint8_t*)mmap(NULL, size, PROT_READ|PROT_WRITE, flags, m_fd, 0);

                if ( m_buffer == MAP_FAILED) {
                    throw 2;
                }
            }
            ~Backing() {
                munmap( m_buffer, m_size );
                if ( -1 != m_fd ) {
                    close( m_fd );
                }
            }
            void set( uint64_t addr, uint8_t value ) {
                m_buffer[addr - m_offset ] = value;
            }
            uint8_t get(uint64_t addr ) {
                return m_buffer[addr - m_offset];
            }

        private:
            uint8_t* m_buffer;
            int m_fd;

            int m_size;
            size_t m_offset;
        };

        class c_MemhBridgeContent: public c_MemhBridge {


        public:
            c_MemhBridgeContent(SST::ComponentId_t x_id, SST::Params& x_params);
            ~c_MemhBridgeContent();


        private:
//Backing*       backing_;
            uint8_t*       backing_;
            c_CompressEngine* m_compEngine;

            void createTxn();
            void handleContentEvent(SST::Event *ev);
        //    void readResponse(); //read from res q to output

        //    void printTxn(bool isWrite, uint64_t addr);

            //Debug
        //    Output *output;


            //link to/from CPU
        //    SST::Link *m_linkCPU;
            SST::Link *m_contentLink;

        //    bool k_printTxnTrace;
        //    std::string k_txnTraceFileName;
        //    std::filebuf m_txnTraceFileBuf;
        //    std::streambuf *m_txnTraceStreamBuf;
        //    std::ofstream m_txnTraceOFStream;
        //    std::ostream *m_txnTraceStream;
        };
    } // namespace n_Bank
} // namespace SST

#endif  /* _TXNGENRAND_H */
