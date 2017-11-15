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

        class c_MemhBridgeContent: public c_MemhBridge {


        public:
            c_MemhBridgeContent(SST::ComponentId_t x_id, SST::Params& x_params);
            ~c_MemhBridgeContent();


        private:
            uint8_t*       backing_;
            uint64_t m_backing_size;
            bool loopback_en;
 
            c_CompressEngine* m_compEngine;


            void createTxn();
            void handleContentEvent(SST::Event *ev);


            SST::Link *m_contentLink;


            // Statistics
            std::map<int, uint64_t> m_normalized_size;

        };
    } // namespace n_Bank
} // namespace SST

#endif  /* _TXNGENRAND_H */
