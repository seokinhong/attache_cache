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
#include <zlib.h>

//local includes
#include "c_Transaction.hpp"
#include "c_MemhBridge.hpp"
#include "c_TxnGen.hpp"
#include "c_CompressEngine.hpp"




namespace SST {
    namespace n_Bank {

        class c_MemhBridgeContent: public c_MemhBridge {


        public:
            class c_Cacheline{
            public:
                c_Cacheline(std::vector<uint8_t> &new_data)
                {
                    char size=new_data.size();
                    data=(uint8_t*)malloc(sizeof(uint8_t*)*size);
                    for(int i=0;i<size;i++)
                    {
                        *(data+i)=new_data[i];
                    }
                }

                ~c_Cacheline(){
                    delete data;
                }

                uint8_t* getData()
                {

                    return data;
                }

            private:
                uint8_t *data;
            };

            c_MemhBridgeContent(SST::ComponentId_t x_id, SST::Params& x_params);
            ~c_MemhBridgeContent();
            std::vector<SST::Link*> m_laneLinks;
            void init(unsigned int phase);


        private:

            bool loopback_en;
            std::map<uint32_t, uint64_t> m_inst_num;   //global instruction count

            c_CompressEngine* m_compEngine;


            void createTxn();
            void handleContentEvent(SST::Event *ev);
            void storeContent(SST::Event *ev);

            void printTxn(uint64_t x_thread_id, bool x_isWrite, uint64_t x_addr,uint64_t x_inst_num,uint32_t compratio_bdi);
            void printTxn(uint64_t x_cycle, uint64_t x_thread_id, bool x_isWrite, uint64_t x_addr,uint64_t x_inst_num);

            SST::Link *m_contentLink;

            std::map<uint64_t, c_Cacheline*> backing_;
            std::map<uint64_t, uint32_t> compratio_bdi;
            std::map<uint64_t, uint32_t> compratio_fpc;
            std::map<uint64_t, uint32_t> compratio_fvc;

            int verbosity;

            int m_numThread;
            bool k_traceDebug;			//enable to print a text-mode trace file
            bool k_loopback_mode;			//enable the loopback mode
            bool boolStoreContent;
            bool boolStoreCompRate;
            std::map<COMP_ALG, uint64_t> m_sumCompRate;
            uint64_t m_cntReqs;
            std::vector<gzFile> traceZ;



            // Statistics
            std::map<int, uint64_t> m_normalized_size;


            /*Statistic<double>* s_CompRatio;
            Statistic<uint64_t>* s_RowSize0;
            Statistic<uint64_t>* s_RowSize25;
            Statistic<uint64_t>* s_RowSize50;
            Statistic<uint64_t>* s_RowSize75;
            Statistic<uint64_t>* s_RowSize100;
            Statistic<uint64_t>* s_CachelineSize50;
            Statistic<uint64_t>* s_CachelineSize100;
            Statistic<uint64_t>* s_BackingMiss;
            Statistic<uint64_t>* s_DoubleRankAccess;
            Statistic<uint64_t>* s_SingleRankAccess;
            Statistic<uint64_t>* s_MemzipMetaCacheHit;
            Statistic<uint64_t>* s_MemzipMetaCacheMiss;
            Statistic<uint64_t>* s_predicted_fail_below50;
            Statistic<uint64_t>* s_predicted_success_above50;
            Statistic<uint64_t>* s_predicted_success_below50;
            Statistic<uint64_t>* s_predicted_fail_above50;*/

        };
    } // namespace n_Bank
} // namespace SST

#endif  /* _TXNGENRAND_H */
