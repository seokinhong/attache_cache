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

// Copyright 2016 IBM Corporation

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//   http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef C_CMDSCHEDULER_H
#define C_CMDSCHEDULER_H

#include <queue>
#include <fstream>
// SST includes

#include <sst/core/subcomponent.h>
#include <sst/core/component.h>
#include <sst/core/link.h>

// local includes
#include "c_BankCommand.hpp"

using namespace std;
namespace SST {
    namespace n_Bank {
        class c_CmdUnit;

        template<class I, class O>
        class c_CtrlSubComponent : public SST::SubComponent {


        public:
            c_CtrlSubComponent(Component *comp, Params &x_params);
            ~c_CtrlSubComponent() {};

            void finish() {};
            void print() {}; // print internals

            //interfaces
            void push(I input);
            int getToken();

        protected:
            enum DEBUG_MASK{
                TXNCVT = 1,
                CMDSCH = 1<<1,
                ADDRHASH= 1<<2,
                DVCCTRL=1<<3
            };
            void debug(unsigned mask_bit, unsigned debug_level, const char* format, ...);
            void debug(const char* prefix, unsigned mask_bit, unsigned debug_level, const char* format, ...);
            unsigned parseDebugFlags(std::string debugFlags);
            bool isDebugEnabled(DEBUG_MASK x_debug_mask);


            //internal functions
            virtual void run() =0 ;
            virtual void send(){};

            // internal buffer
            std::deque<I> m_inputQ;                    //input queue
            std::deque<O> m_outputQ;

            // params for internal architecture
            int k_numCtrlIntQEntries;

            // debug output
            Output*         m_debugOutput;
            unsigned        m_debug_bits;
            bool m_debug_en;
            std::vector<std::string>m_debugFlags;


        };

        template<class I, class O>
        c_CtrlSubComponent<I, O>::c_CtrlSubComponent(Component *owner, Params &x_params) : SubComponent(owner) {

            //set debug output
            unsigned l_debug_level = x_params.find<uint32_t>("debug_level", 0);
            std::string l_debug_flag = x_params.find<std::string>("debug_flag","");
            m_debug_bits = parseDebugFlags(l_debug_flag);
            m_debugOutput = new SST::Output("",
                                            l_debug_level,
                                            m_debug_bits,
                                            (Output::output_location_t)x_params.find<int>("debug_location", 1),
                                            x_params.find<std::string>("debug_file","debugLog"));

            if(l_debug_level>0 && m_debug_bits!=0)
                m_debug_en=true;
            else
                m_debug_en=false;

            m_inputQ.clear();
            m_outputQ.clear();
            bool l_found = false;



            // calculate total number of banks
         /*   m_numChannelsPerDimm = (uint32_t)x_params.find<uint32_t>("numChannelsPerDimm", 1,
                                                                         l_found);
            if (!l_found) {
                std::cout << "numChannelsPerDimm value is missing... exiting"
                          << std::endl;
                exit(-1);
            }



            m_numRanksPerChannel = (uint32_t)x_params.find<uint32_t>("numRanksPerChannel", 100,
                                                                         l_found);
            if (!l_found) {
                std::cout << "numRanksPerChannel value is missing... exiting"
                          << std::endl;
                exit(-1);
            }

            m_numBankGroupsPerRank = (uint32_t)x_params.find<uint32_t>("numBankGroupsPerRank",
                                                                           100, l_found);
            if (!l_found) {
                std::cout << "numBankGroupsPerRank value is missing... exiting"
                          << std::endl;
                exit(-1);
            }

            m_numBanksPerBankGroup = (uint32_t)x_params.find<uint32_t>("numBanksPerBankGroup",
                                                                           100, l_found);
            if (!l_found) {
                std::cout << "numBanksPerBankGroup value is missing... exiting"
                          << std::endl;
                exit(-1);
            }*/





            k_numCtrlIntQEntries = (uint32_t) x_params.find<uint32_t>("numCtrlIntQEntries", 100, l_found);
            if (!l_found) {
                std::cout << "k_numCtrlIntQEntries value is missing..." << std::endl;
            }
        }


        template<class I, class O>
        void c_CtrlSubComponent<I, O>::push(I input) {
            m_inputQ.push_back(input);
            //std::cout<<"m_cmdReqQ.size():"<<m_cmdReqQ.size()<<std::endl;
        }

        template<class I, class O>
        int c_CtrlSubComponent<I, O>::getToken() {
            int l_QueueSize = m_inputQ.size();
            assert(k_numCtrlIntQEntries >= l_QueueSize);

            return k_numCtrlIntQEntries - l_QueueSize;
        }

        template<class I, class O>
        void c_CtrlSubComponent<I, O>::debug(unsigned mask_bit, unsigned debug_level, const char* format, ...)
        {
           // m_debugOutput->verbosePrefix(prefix.c_str(),CALL_INFO,3,mask_bit,msg.c_str());
            if(m_debug_en==true) {
                va_list args;
                va_start(args,format);
                size_t size = std::snprintf(nullptr, 0, format, args)+ 1;
                std::unique_ptr<char[]> buf(new char[size]);
                std::vsnprintf(buf.get(),size, format, args);
                std::string msg = std::string(buf.get(),buf.get()+size -1);

                m_debugOutput->verbose(CALL_INFO, debug_level, mask_bit, msg.c_str());
                m_debugOutput->flush();
                va_end(args);
            }
        }

        template<class I, class O>
        void c_CtrlSubComponent<I, O>::debug(const char* prefix, unsigned mask_bit, unsigned debug_level, const char* format, ...) {
            // m_debugOutput->verbosePrefix(prefix.c_str(),CALL_INFO,3,mask_bit,msg.c_str());
            if (m_debug_en == true) {
                va_list args;
                va_start(args, format);
                size_t size = std::snprintf(nullptr, 0, format, args) + 1;
                std::unique_ptr<char[]> buf(new char[size]);
                std::vsnprintf(buf.get(), size, format, args);
                std::string msg = std::string(buf.get(), buf.get() + size - 1);

                m_debugOutput->verbosePrefix(prefix, CALL_INFO, debug_level, mask_bit, msg.c_str());
                m_debugOutput->flush();
                va_end(args);
            }
        }



            template<class I, class O>
        unsigned c_CtrlSubComponent<I, O>::parseDebugFlags(std::string x_debugFlags)
        {

            unsigned debug_bits=0;
            std::string delimiter = ",";
            size_t pos=0;
            std::string token;
			if(x_debugFlags!="")
			{
				while((pos=x_debugFlags.find(delimiter)) != std::string::npos){
					token = x_debugFlags.substr(0,pos);
					m_debugFlags.push_back(token);
					x_debugFlags.erase(0,pos+delimiter.length());
				}
				m_debugFlags.push_back(x_debugFlags);

				for(auto &it : m_debugFlags)
				{
					std::cout <<it<<std::endl;
					if(it=="dvcctrl")
							debug_bits|=DVCCTRL;
					else if(it=="txncvt")
							debug_bits|=TXNCVT;
					else if(it=="cmdsch")
							debug_bits|=CMDSCH;
					else if(it=="addrhash")
						debug_bits|=ADDRHASH;
					else {
						printf("debug flag error! (dvcctrl/txncvt/cmdsch/addrhash)\n");
						exit(1);
					}
				}
				return debug_bits;
			}
			return 0;
        }

        template<class I, class O>
        bool c_CtrlSubComponent<I, O>::isDebugEnabled(DEBUG_MASK x_debug_mask)
        {
            if(m_debug_bits & x_debug_mask)
                return true;
            else
                return false;
        };
    }
}
#endif //SRC_C_CMDSCHEDULER_H