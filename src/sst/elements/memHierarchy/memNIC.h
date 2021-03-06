// Copyright 2013-2017 Sandia Corporation. Under the terms
// of Contract DE-NA0003525 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2013-2017, Sandia Corporation
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef _MEMHIERARCHY_MEMNIC_SUBCOMPONENT_H_
#define _MEMHIERARCHY_MEMNIC_SUBCOMPONENT_H_

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>

#include <sst/core/event.h>
#include <sst/core/output.h>
#include <sst/core/subcomponent.h>
#include <sst/core/interfaces/simpleNetwork.h>

#include "sst/elements/memHierarchy/memEventBase.h"
#include "sst/elements/memHierarchy/memEvent.h"
#include "sst/elements/memHierarchy/util.h"
#include "sst/elements/memHierarchy/memLinkBase.h"

namespace SST {
namespace MemHierarchy {

/*
 *  MemNIC provides a simpleNetwork (from SST core) network interface for memory components
 *  and overlays memory functions on top
 *
 *  The memNIC assumes each network endpoint is associated with a set of memory addresses and that
 *  each endpoint communicates with a subset of endpoints on the network as defined by "sources"
 *  and "destinations".
 *
 */
class MemNIC : public MemLinkBase {

public:
    
    /* Constructor */
    MemNIC(Component * comp, Params &params);
    
    /* Destructor */
    ~MemNIC() { }

    /* Functions called by parent for handling events */
    bool clock();
    void send(MemEventBase * ev);
    MemEventBase * recv();
    
    /* Callback to notify when link_control receives a message */
    bool recvNotify(int);

    /* Helper functions */
    size_t getSizeInBits(MemEventBase * ev);
    uint64_t lookupNetworkAddress(const std::string &dst) const;

    /* Initialization and finish */
    void init(unsigned int phase);
    void sendInitData(MemEventInit * ev);
    MemEventInit* recvInitData();
    void finish() { link_control->finish(); }
    void setup() { link_control->setup(); MemLinkBase::setup(); }

    // Router events
    class MemRtrEvent : public SST::Event {
        public:
            MemEventBase * event;
            MemRtrEvent() : Event(), event(nullptr) { }
            MemRtrEvent(MemEventBase * ev) : Event(), event(ev) { }

            virtual Event* clone(void) override {
                MemRtrEvent *mre = new MemRtrEvent(*this);
                mre->event = this->event->clone();
                return mre;
            }

            virtual bool hasClientData() const { return true; }

            void serialize_order(SST::Core::Serialization::serializer &ser) override {
                Event::serialize_order(ser);
                ser & event;
            }

            ImplementSerializable(SST::MemHierarchy::MemNIC::MemRtrEvent);
    };

    class InitMemRtrEvent : public MemRtrEvent {
        public:
        EndpointInfo info;

        InitMemRtrEvent() {}
        InitMemRtrEvent(EndpointInfo info) : MemRtrEvent(), info(info) { }
        
        virtual Event* clone(void) override {
            return new InitMemRtrEvent(*this);
        }

        virtual bool hasClientData() const override { return false; }

        void serialize_order(SST::Core::Serialization::serializer & ser) override {
            MemRtrEvent::serialize_order(ser);
            ser & info.name;
            ser & info.addr;
            ser & info.id;
            ser & info.region.start;
            ser & info.region.end;
            ser & info.region.interleaveSize;
            ser & info.region.interleaveStep;
        }

        ImplementSerializable(SST::MemHierarchy::MemNIC::InitMemRtrEvent);
    };
    
    bool isSource(std::string str) { /* Note this is only used during init so doesn't need to be fast */
        for (std::set<EndpointInfo>::iterator it = sourceEndpointInfo.begin(); it != sourceEndpointInfo.end(); it++) {
            if (it->name == str) return true;   
        }
        return false;
    }
    bool isDest(std::string str) { /* Note this is only used during init so doesn't need to be fast */
        for (std::set<EndpointInfo>::iterator it = destEndpointInfo.begin(); it != destEndpointInfo.end(); it++) {
            if (it->name == str) return true;   
        }
        return false;
    }

private:

    // Other parameters
    size_t packetHeaderBytes;
    bool initMsgSent;

    // Handlers and network
    SST::Interfaces::SimpleNetwork *link_control;

    // Data structures
    std::unordered_map<std::string,uint64_t> networkAddressMap;         // Map of name -> address for each network endpoint

    // Event queues
    std::queue<MemRtrEvent*> initQueue; // Queue for received init events
    std::queue<SST::Interfaces::SimpleNetwork::Request*> initSendQueue; // Queue of events waiting to be sent. Sent after merlin initializes
    std::queue<SST::Interfaces::SimpleNetwork::Request*> sendQueue; // Queue of events waiting to be sent (sent on clock)
};

} //namespace memHierarchy
} //namespace SST

#endif
