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

// Copyright 2015 IBM Corporation

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//   http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef C_CONTROLLERPCA_HPP
#define C_CONTROLLERPCA_HPP


// SST includes
#include <sst/core/link.h>
#include <sst/core/component.h>
#include "c_Controller.hpp"
#include "c_CompressEngine.hpp"
#include "Cache.hpp"
#include <sst/elements/memHierarchy/memEvent.h>
#include <sst/elements/memHierarchy/membackend/backing.h>
#include <map>
#include <vector>

#define MAX_COLUMN_NUM 128

#include <math.h>

using namespace SST::CACHE;
// local includes
namespace SST {
    namespace n_Bank {

        class c_MetaCache{

        public:
            c_MetaCache(int _row_size, int num_row, int num_way, Output* output, double _hit_rate)   //row size(unit):
            {
                m_output=output;
                m_hit_rate=_hit_rate;
                m_num_way=num_way;

                index_size=log2(num_row);
                page_offset_size=log2(_row_size);
                index_mask=pow((double)2,(double)index_size)-1;
                m_hit_cnt=0;
                m_miss_cnt=0;

                printf("[Memzip meta cache] memory row size: %d # of metacache row: %d # of way: %d, index_size: %d page_offset_size: %d page_mask:%x metacache_hitrate:%d\n",
                       _row_size, num_row, num_way, index_size, page_offset_size, index_mask,m_hit_rate);

                for(int i=0; i<num_row;i++)
                {
                    std::deque<uint64_t> *tag_entry=new std::deque<uint64_t>();

                    for(int i=0;i<num_way;i++)
                    {
                        tag_entry->push_back(-1);
                    }

                    tagarray.push_back(tag_entry);
                }
            }

            bool isHit(uint64_t addr)  //row address is used for index
            {
                int randvalue = rand()%100;
                bool hit=false;


                //constant hit rate
                if(m_hit_rate>0) {

                    if(randvalue <= m_hit_rate)
                        hit= true;
                    else
                        hit= false;

                }//actual cache model
                else {
                    uint64_t tag = getTag(addr);
                    uint64_t index =  getIndex(addr);

                    //  m_output->verbose(CALL_INFO, 2, 0, "[Memzip meta cache] addr: %llx tag: %x index: %d\n", addr,
                    //                  tag, index);

                    //tag matching
                    std::deque<uint64_t> *tag_entry=tagarray[index];

                    /*    printf("\n");
                        printf("[before access addr:%llx index:%llx tag:%llx\t]",addr, index,tag);
                        for(auto &it:*tag_entry)
                        {
                            printf("tag:%lld\t",it);
                        }
                        printf("\n");*/

                    for(std::deque<uint64_t>::iterator tag_it=tag_entry->begin();tag_it!=tag_entry->end();tag_it++)
                    {
                        //hit
                        uint64_t l_tag=*tag_it;
                        if ((l_tag == tag)) {
                            hit = true;
                            tag_entry->erase(tag_it);
                            tag_entry->push_front(tag);
                            break;
                        }//miss
                        else {
                            hit = false;
                        }
                    }

                    assert(tag_entry->size()==m_num_way);
                    if (hit) {
                        m_output->verbose(CALL_INFO, 2, 0, "[Memzip meta cache hit] addr: %llx tag: %x index: %d\n",
                                          addr, tag, index);
                        m_hit_cnt++;


                        /* printf("[after hit index:%lld tag:%llx\t]",index, tag);
                        for(auto &it:*tag_entry)
                          {
                           printf("tag:%lld\t",it);
                              }
                         printf("\n");*/

                    }
                    else {
                        m_output->verbose(CALL_INFO, 2, 0,
                                          "[Memzip meta cache miss] addr: %llx tag: %x index: %d\n", addr, tag,
                                          index);
                        m_miss_cnt++;
                    }
                }
                return hit;
            }

            uint64_t getTag(uint64_t addr) {
                uint64_t tag = addr >> (index_size + page_offset_size);
                return tag;
            }
            uint64_t getIndex(uint64_t addr){
                uint64_t index = (addr >> page_offset_size) & index_mask;
                return index;
            }

            uint64_t fill(uint64_t addr)
            {
                uint64_t tag=getTag(addr);
                uint64_t index=getIndex(addr);
                uint64_t evict_tag = tagarray[index]->back();
                uint64_t evict_addr = (evict_tag<<index_size+index)<<page_offset_size;  //assume that meta data is stored in the first column of each row

                tagarray[index]->push_front(tag);
                tagarray[index]->pop_back();

                m_output->verbose(CALL_INFO,2,0,"[Memzip meta chache fill] addr: %llx tag: %llx index:%d evict_addr: %llx\n",
                                  addr,tag,index,evict_addr);


                assert(tagarray[index]->size()==m_num_way);
                return evict_addr;
            }

            uint64_t fill(uint64_t addr,uint64_t data)
            {
                uint64_t evict_addr=fill(addr);
                data_array[addr]=data;
                data_array[evict_addr]=0;
                return evict_addr;
            }

            uint64_t getData(uint64_t addr)
            {
                if(data_array.find(addr)==data_array.end()) {
                    printf("error!!, fail to find the data in the data array\n");
                    exit(-1);
                }
                else
                    return data_array[addr];
            }

            uint64_t getMetaDataAddress(uint64_t addr, bool is_write)
            {
                uint64_t metadata_address=0;
                uint64_t mask = memsize-1;

                if(is_write)
                    metadata_address=rand() & mask;
                else
                    metadata_address=addr & mask;

                // return addr;
                return metadata_address;
            }

            uint64_t getHitCnt(){return m_hit_cnt;}
            uint64_t getMissCnt(){return m_miss_cnt;}
        private:
            int m_hit_rate;
            int index_size;
            int page_offset_size;
            int index_mask;

            std::vector<std::deque<uint64_t>*> tagarray;
            std::map<uint64_t, uint64_t> data_array;
            uint64_t m_hit_cnt;
            uint64_t m_miss_cnt;
            Output *m_output;
            uint64_t memsize;
            int m_num_way;
        };


        class c_2LvPredictor{
        public:
            uint64_t getHitCnt(){return m_hit_cnt;}
            uint64_t getMissCnt(){return m_miss_cnt;}
            uint64_t getClHitCnt(){return m_cl_hit_cnt;}
            uint64_t getClMissCnt(){return m_cl_miss_cnt;}
            uint64_t getPredSuccessCnt(){return m_predSucess_cnt;}
            uint64_t getPredFailCnt(){return m_predFail_cnt;}

            c_2LvPredictor(uint64_t robr_entries, int lipr_entries, int _num_col_per_lipr_entry,bool isSelectiveReplace_, Output* output);
            c_2LvPredictor(uint64_t global_predictor_entries, uint64_t robr_entries, int lipr_entries, int _num_rows_rowtable, Output* output);
            uint8_t updateRowTable(int cacheline_size, int row_);
            int getPredictedSize(uint32_t col, uint32_t row_, uint32_t actual_size);
            int getCompSizeFromCache(int col, int row);
            int update(int col, int row, int compSize);
            uint64_t m_predictor_lipr_miss;
            uint64_t m_predictor_lipr_hit;
            uint64_t m_predictor_lipr_success;
            uint64_t m_predictor_lipr_fail;
            uint64_t m_predictor_ropr_success;
            uint64_t m_predictor_ropr_fail;



        private:
            std::vector<uint8_t> m_rowtable;
            std::vector<std::vector<std::pair<uint8_t,uint64_t>>> m_cache_data;
            std::vector<uint64_t> m_cache_tag;

            //c_MetaCache m_highAssocRowTable;
            std::vector<uint8_t> m_global_predictor;
            SCache* m_row_predictor;
            SCache* m_line_predictor;

            int m_num_cache_entries;
            int m_num_col_per_cache_entry;
            int cache_index_offset;
            int cache_tag_offset;
            int cache_index_mask;
            int row_table_offset;
            int m_row_table_mask;
            int m_dram_column;
            bool isSelectiveReplace;
            bool isHighAssocRowTable;
            uint64_t m_hit_cnt;
            uint64_t m_miss_cnt;
            uint64_t m_cl_hit_cnt;
            uint64_t m_cl_miss_cnt;
            uint64_t m_predSucess_cnt;
            uint64_t m_predFail_cnt;

            Output* m_output;
        };

        class c_2LvPredictor_new{
        public:
            uint64_t getHitCnt(){return m_hit_cnt;}
            uint64_t getMissCnt(){return m_miss_cnt;}
            uint64_t getClHitCnt(){return m_cl_hit_cnt;}
            uint64_t getClMissCnt(){return m_cl_miss_cnt;}
            uint64_t getPredSuccessCnt(){return m_predSucess_cnt;}
            uint64_t getPredFailCnt(){return m_predFail_cnt;}

            c_2LvPredictor_new(uint64_t global_predictor_entries, uint64_t robr_entries, int lipr_entries, int pageSize, int global_predictor_thres, Output* output);
            int getPredictedSize(uint64_t addr, uint32_t actual_size);
            uint64_t m_predictor_lipr_miss;
            uint64_t m_predictor_lipr_hit;
            uint64_t m_predictor_lipr_success;
            uint64_t m_predictor_lipr_fail;
            uint64_t m_predictor_ropr_miss;
            uint64_t m_predictor_ropr_hit;
            uint64_t m_predictor_ropr_success;
            uint64_t m_predictor_ropr_fail;
            uint64_t m_predictor_global_success;
            uint64_t m_predictor_global_fail;



        private:
            std::vector<uint8_t> m_rowtable;
            std::vector<std::vector<std::pair<uint8_t,uint64_t>>> m_cache_data;
            std::vector<uint64_t> m_cache_tag;

            //c_MetaCache m_highAssocRowTable;
            std::vector<uint8_t> m_global_predictor;
            int m_global_predictor_mask;
            uint64_t m_global_prediction_threadhold;
            SCache* m_row_predictor;
            SCache* m_line_predictor;
            std::map<uint64_t, std::vector<uint8_t>> m_row_predictor_data;
            int m_ropr_col_offset_size;
            int m_ropr_col_num;

            int m_page_offset_size;
            int m_num_cache_entries;
            int m_num_col_per_cache_entry;
            int cache_index_offset;
            int cache_tag_offset;
            int cache_index_mask;
            int row_table_offset;
            int m_row_table_mask;
            int m_dram_column;
            bool isSelectiveReplace;
            bool isHighAssocRowTable;
            uint64_t m_hit_cnt;
            uint64_t m_miss_cnt;
            uint64_t m_cl_hit_cnt;
            uint64_t m_cl_miss_cnt;
            uint64_t m_predSucess_cnt;
            uint64_t m_predFail_cnt;
            uint64_t m_pageSize;

            Output* m_output;
        };



        class c_RowStat{
        private:
            uint64_t size_25;
            uint64_t size_50;
            uint64_t size_75;
            uint64_t size_100;
            uint64_t m_access_count;
            uint8_t *column_size;
        public:

            int record(int normalized_size, int col)
            {
                //m_cl_normalized_size[normalized_size]++;
             //   m_col_access_count[col]++;
                if(normalized_size<=25)
                    size_25++;
                else if(normalized_size<=50)
                    size_50++;
                else if(normalized_size<=75)
                    size_75++;
                else
                    size_100++;
                column_size[col]=normalized_size;

                m_access_count++;
            }

            uint64_t get_normalized_size_cnt(int normalized_size){
                  if(normalized_size<=25)
                      return size_25;
                  else if(normalized_size<=50)
                      return size_50;
                  else if(normalized_size<=75)
                      return size_75;
                  else
                    return size_100;
            }

            /*uint64_t get_col_access_cnt(int col){
                return m_col_access_count[col];
            }*/

            uint64_t get_access_cnt(){
                return m_access_count;
            }


            c_RowStat(int col_num) {
               // m_col_access_count.resize(col_num);
                column_size=(uint8_t*) calloc(sizeof(uint8_t),col_num);
                m_access_count=0;
                size_25=0;
                size_50=0;
                size_75=0;
                size_100=0;
            }

            ~c_RowStat();
        };

        class c_ControllerPCA : public c_Controller {

        public:
            bool clockTic(SST::Cycle_t clock);
            c_ControllerPCA(SST::ComponentId_t id, SST::Params &params);
            void finish();
            void init(unsigned int phase);
            std::vector<SST::Link*> m_laneLinks;
            ~c_ControllerPCA();
            bool isMemzipMode(){return memzip_mode;}


        private:
            uint64_t isMultiThreadMode;
            std::map<uint64_t, uint64_t> pageTable;
            uint64_t m_metacache_latency;
            bool multilane_rowtable;
            //uint8_t*       backing_;
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





           // std::map<uint64_t, uint8_t> backing_;
            std::map<uint64_t, uint8_t> compRatio_bdi;
            std::map<uint64_t, uint8_t> compRatio_fvc;
            std::map<uint64_t, uint8_t> compRatio_fpc;

            //c_MetaCache* metacache;
            SST::CACHE::SCache * metacache;
            std::map<uint64_t, bool> m_metacache_data;
            uint64_t m_metacache_update_cnt;
            uint64_t m_mcache_evict_cnt;
            uint64_t m_mcache_wb_cnt;

            c_2LvPredictor* cmpSize_predictor;
            c_2LvPredictor_new* cmpSize_predictor_new;


            uint64_t m_backing_size;
            bool loopback_en;
            bool compression_en;
            bool pca_mode;
            bool oracle_mode;
            bool m_isFixedCompressionMode;
            int verbosity;
            uint64_t m_nextPageAddress;
            uint64_t m_osPageSize;



            uint64_t m_total_num_banks;
            uint64_t m_chnum;
            uint64_t m_ranknum;
            uint64_t m_bgnum;
            uint64_t m_banknum;
            uint64_t m_rownum;
            uint64_t m_colnum;
            uint64_t m_memsize;

            c_CompressEngine* m_compEngine;
            SST::Link *m_contentLink;
            int metadata_predictor;
            bool no_metadata;
            bool memzip_mode;

            // Statistics
            std::map<int, uint64_t> m_normalized_size;
            //std::vector<std::vector<c_RowStat*>> m_row_stat;
            std::map<uint32_t, std::map<uint32_t, c_RowStat*>> m_row_stat;
            void handleContentEvent(SST::Event *ev);
            void storeContent();
            uint32_t getPageAddress();
            void handleInDeviceResPtrEvent(SST::Event *ev);

            Statistic<double>* s_CompRatio;
            Statistic<uint64_t>* s_simCycles;
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
            Statistic<uint64_t>* s_predicted_fail_above50;
            Statistic<uint64_t>* s_predictor_lipr_hit;
            Statistic<uint64_t>* s_predictor_lipr_miss;
            Statistic<uint64_t>* s_predictor_ropr_hit;
            Statistic<uint64_t>* s_predictor_ropr_miss;
            Statistic<uint64_t>* s_predictor_lipr_success;
            Statistic<uint64_t>* s_predictor_lipr_fail;
            Statistic<uint64_t>* s_predictor_ropr_success;
            Statistic<uint64_t>* s_predictor_ropr_fail;
            Statistic<uint64_t>* s_predictor_global_success;
            Statistic<uint64_t>* s_predictor_global_fail;
            Statistic<uint64_t>* s_metacache_data_update;
            Statistic<uint64_t>* s_metacache_evict;
            Statistic<uint64_t>* s_metacache_wb;






        };


    }
}


#endif //C_CONTROLLER_HPP
