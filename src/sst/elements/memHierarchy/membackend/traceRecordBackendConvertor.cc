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


#include <sst_config.h>
#include <sst/core/params.h>
#include "sst/elements/memHierarchy/util.h"
#include "membackend/traceRecordBackendConvertor.h"
#include "membackend/traceRecordBackend.h"

using namespace SST;
using namespace SST::MemHierarchy;

traceRecordBackendConvertor::traceRecordBackendConvertor(Component *comp, Params &params) :
        MemBackendConvertor(comp,params) 
{
    using std::placeholders::_1;
    static_cast<traceRecordBackend*>(m_backend)->setResponseHandler( std::bind( &traceRecordBackendConvertor::handleMemResponse, this, _1 ) );
}

bool traceRecordBackendConvertor::issue( MemReq* req ) {
   /* if(req->detail)
        return static_cast<SimpleMemBackend*>(m_backend)->issueRequest( req->id(), req->addr(), req->isWrite(), m_backendRequestWidth,req->m_thread_id,req->m_ip);
    else*/

    std::vector<uint64_t> data;
    data.push_back(req->m_thread_id);
    data.push_back(req->m_ip);
    data.push_back(req->m_compRate);

    return static_cast<traceRecordBackend*>(m_backend)->issueRequest
            ( req->id(), req->addr(), req->isWrite(), m_backendRequestWidth,data);
}
