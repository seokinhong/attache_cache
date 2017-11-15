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
#include <sst/elements/memHierarchy/memEvent.h>
#include <sst/elements/memHierarchy/membackend/backing.h>

#define MAX_COLUMN_NUM 128

#include <math.h>
// local includes
namespace SST {
    namespace n_Bank {

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
            ~c_ControllerPCA();


        private:
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

            class c_MetaCache{

            public:
                c_MetaCache(int _row_size, int num_entry, Output* output, double _hit_rate)   //row size(unit):
                {
                    m_output=output;
                    m_hit_rate=_hit_rate;

                    index_size=log2(num_entry);
                    page_offset_size=log2(_row_size);
                    page_mask=pow((double)2,(double)index_size)-1;
                    m_hit_cnt=0;
                    m_miss_cnt=0;

                    printf("[Memzip meta cache] row_size: %d num_entry: %d index_size: %d page_offset_size: %d page_mask:%x metacache_hitrate:%d\n",
                           _row_size, num_entry, index_size, page_offset_size, page_mask,m_hit_rate);

                    tagarray.resize(num_entry);
                }

                bool isHit(uint64_t addr)  //row address is used for index
                {
                    int randvalue = rand()%100;
                    bool hit=false;

                    if(m_hit_rate>0) {

                        if(randvalue <= m_hit_rate)
                            hit= true;
                        else
                            hit= false;

                    } else {
                        uint64_t tag = addr >> (index_size + page_offset_size);
                        uint64_t index = (addr >> page_offset_size) & page_mask;

                        m_output->verbose(CALL_INFO, 2, 0, "[Memzip meta cache] addr: %llx tag: %x index: %d\n", addr,
                                          tag, index);
                        if ((tagarray[index] == tag)) {
                            m_output->verbose(CALL_INFO, 2, 0, "[Memzip meta cache hit] addr: %llx tag: %x index: %d\n",
                                              addr, tag, index);
                            hit = true;
                        } else {
                            m_output->verbose(CALL_INFO, 2, 0,
                                              "[Memzip meta cache miss] addr: %llx tag: %x index: %d\n", addr, tag,
                                              index);
                            hit = false;
                        }
                        if (hit)
                            m_hit_cnt++;
                        else
                            m_miss_cnt++;
                    }

                    return hit;
                }


                uint64_t fill(uint64_t addr)
                {
                    uint64_t tag=addr>>(index_size+page_offset_size);
                    uint64_t index=(addr>>page_offset_size)&page_mask;
                    uint64_t evict_addr=(tagarray[index]<<index_size+index)<<page_offset_size;

                    tagarray[index]=tag;

                    m_output->verbose(CALL_INFO,2,0,"[Memzip meta chache fill] addr: %llx tag: %llx index:%d evict_addr: %llx\n",
                           addr,tag,index,evict_addr);
                    return evict_addr;
                }

                uint64_t getMetaDataAddress(uint64_t addr)
                {
                    uint64_t metaArraySize=32*1024*1024;
                    uint64_t mask = ~(metaArraySize-1);
                    return addr;
                    return rand()& mask;
                }
                uint64_t getHitCnt(){return m_hit_cnt;}
                uint64_t getMissCnt(){return m_miss_cnt;}
            private:
                int m_hit_rate;
                int index_size;
                int page_offset_size;
                int page_mask;
                std::vector<uint64_t> tagarray;
                uint64_t m_hit_cnt;
                uint64_t m_miss_cnt;
                Output *m_output;
            };

            class c_2LvPredictor{
            public:
                uint64_t getHitCnt(){return m_hit_cnt;}
                uint64_t getMissCnt(){return m_miss_cnt;}
                uint64_t getClHitCnt(){return m_cl_hit_cnt;}
                uint64_t getClMissCnt(){return m_cl_miss_cnt;}
                uint64_t getPredSuccessCnt(){return m_predSucess_cnt;}
                uint64_t getPredFailCnt(){return m_predFail_cnt;}
                c_2LvPredictor(uint64_t rownum, int _num_cache_entries, int _num_col_per_cache_entry, Output* output)
                {
                        for(int j=0; j<rownum;j++)
                            m_rowtable.push_back(0);

                    m_num_cache_entries=_num_cache_entries;
                    m_num_col_per_cache_entry=_num_col_per_cache_entry;

                    m_cache_data.resize(m_num_cache_entries);
                    m_cache_tag.resize(m_num_cache_entries);
                    for(int i=0;i<m_num_cache_entries;i++) {
                        m_cache_tag.push_back(0);
                        for (int j=0; j < m_num_col_per_cache_entry; j++) {
                            std::pair<uint8_t, uint64_t> l_par = make_pair(0, 0);
                            m_cache_data[i].push_back(l_par);
                        }
                    }

                    cache_index_mask = m_num_cache_entries-1;
                    cache_tag_offset=(int)log2(m_num_cache_entries);
                    m_output=output;
                    m_hit_cnt=0;
                    m_miss_cnt=0;
                  //  cache_tag_offset=(int)log2(_num_col_per_cache_entry);
                }

                void updateRowTable(int cacheline_size, int row)
                {
                    uint8_t prev_state=m_rowtable.at(row);
                    uint8_t new_state=0;
                    if(cacheline_size<=50)
                        new_state=prev_state+1;
                    else
                        new_state=0;

                    if(new_state>3)
                        new_state=3;

                    m_rowtable.at(row)=new_state;
                }

                int getPredictedSize(uint32_t col, uint32_t row, uint32_t actual_size)
                {
                    int predict_size=50;

                    //1. see metacache
                    int tmp_predict_size=getCompSizeFromCache(col, row);
                    if(tmp_predict_size>0) {
                        predict_size=tmp_predict_size;

                        //return predict_size;
                    }
                    else {
                        //2. see rowtable
                        int predict_bit = m_rowtable[row];
                        if (predict_bit == 3)
                            predict_size = 50;
                        else
                            predict_size = 100;

                    }

                    uint64_t pre_success=m_predSucess_cnt;
                    uint64_t pre_fail=m_predFail_cnt;

                        if(actual_size<=50) {
                            if (predict_size <= 50)
                                m_predSucess_cnt++;
                            else
                                m_predFail_cnt++;
                        }
                        else {
                            if(predict_size > 50)
                                m_predSucess_cnt++;
                            else
                                m_predFail_cnt++;
                            }
                  /*  if(pre_success<m_predSucess_cnt)
                        printf("[Success]predsuccess_cnt: %lld predfail_cnt: %lld actual_size:%d pred_size:%d hit:%lld miss:%lld\n",m_predSucess_cnt,m_predFail_cnt,actual_size,predict_size,m_hit_cnt,m_miss_cnt);
                    else
                        printf("[Fail]predsuccess_cnt: %lld predfail_cnt: %lld actual_size:%d pred_size:%d hit:%lld miss:%lld\n",m_predSucess_cnt,m_predFail_cnt,actual_size,predict_size,m_hit_cnt,m_miss_cnt);*/
                    return predict_size;
                }

                int getCompSizeFromCache(int col, int row)
                {
                    int compSize;
                    int index = row & cache_index_mask;
                    int tag = row >> cache_tag_offset;
                    int col_index = (int)((double)col / (double)m_num_col_per_cache_entry);
                    int col_tag = col >> (int)log2(m_num_col_per_cache_entry);

                    if(m_cache_tag.at(index)==tag)
                    {
                        std::pair<uint8_t, uint64_t> comp_data= m_cache_data[index][col_index];

                        if(comp_data.first==col_tag) {
                            compSize = comp_data.second;
                            m_cl_hit_cnt++;
                        }
                        else {
                            m_cl_miss_cnt++;
                            compSize = comp_data.second;
                        }

                        m_hit_cnt++;
                        m_output->verbose(CALL_INFO,1,0,"[Hit] col: %d, row:%d index: %d tag: %d, col_index: %d, col_tag:%d compSize:%d\n",col, row, index, tag, col_index, col_tag, compSize);
                    } else {
                        m_miss_cnt++;
                        m_cl_miss_cnt++;
                        compSize = -1;
                        m_output->verbose(CALL_INFO,1,0,"[Miss] col: %d, row:%d index: %d tag: %d, col_index: %d, col_tag:%d compSize:%d\n",col, row, index, tag, col_index, col_tag, compSize);
                    }


                    return compSize;
                }

//#option 3
/*                int update(int col, int row, int compSize)
                {
                    int index = row & cache_index_mask;
                    int tag = row >> cache_tag_offset;
                    int col_index = (int)((double)col / (double)m_num_col_per_cache_entry);
                    int col_tag = col >> (int)log2(m_num_col_per_cache_entry);

                    //cache miss
                    if(m_cache_tag.at(index)!=tag)
                    {
                            m_cache_tag.at(index) = tag;

                            int confidence_bit = m_rowtable[index];
                            int fill_comp_size = 100;
                            if (confidence_bit == 3)
                                fill_comp_size = compSize;
                            else
                                fill_comp_size = 100;

                            for (int i = 0; i < m_num_col_per_cache_entry; i++) {
                                std::pair<uint8_t, uint64_t> comp_data = make_pair(col_tag, fill_comp_size);
                                m_cache_data[index][i] = comp_data;
                            }

                            std::pair<uint8_t, uint64_t> comp_data = make_pair(col_tag, compSize);
                            m_cache_data[index][col_index] = comp_data;
                    }//cache hit
                    else {
                        std::pair<uint8_t, uint64_t> comp_data = make_pair(col_tag, compSize);
                        m_cache_data[index][col_index] = comp_data;
                    }

                    updateRowTable(compSize,row);
                }
*/

                // option #4, store all rows, and fill the empty slot with the current info
                int update(int col, int row, int compSize)
                {
                    int index = row & cache_index_mask;
                    int tag = row >> cache_tag_offset;
                    int col_index = (int)((double)col / (double)m_num_col_per_cache_entry);
                    int col_tag = col >> (int)log2(m_num_col_per_cache_entry);

                    //cache miss
                    if(m_cache_tag.at(index)!=tag)
                    {
                        m_cache_tag.at(index) = tag;

                        int fill_comp_size = compSize;

                        for (int i = 0; i < m_num_col_per_cache_entry; i++) {
                            std::pair<uint8_t, uint64_t> comp_data = make_pair(col_tag, fill_comp_size);
                            m_cache_data[index][i] = comp_data;
                        }

                        std::pair<uint8_t, uint64_t> comp_data = make_pair(col_tag, compSize);
                        m_cache_data[index][col_index] = comp_data;
                        // }
                    }//cache hit
                    else {
                        std::pair<uint8_t, uint64_t> comp_data = make_pair(col_tag, compSize);
                        m_cache_data[index][col_index] = comp_data;
                    }

                    updateRowTable(compSize,row);
                }


            private:
                std::vector<uint8_t> m_rowtable;
                std::vector<std::vector<std::pair<uint8_t,uint64_t>>> m_cache_data;
                std::vector<uint64_t> m_cache_tag;

                int m_num_cache_entries;
                int m_num_col_per_cache_entry;
                int cache_index_offset;
                int cache_tag_offset;
                int cache_index_mask;
                uint64_t m_hit_cnt;
                uint64_t m_miss_cnt;
                 uint64_t m_cl_hit_cnt;
                uint64_t m_cl_miss_cnt;
                uint64_t m_predSucess_cnt;
                uint64_t m_predFail_cnt;
                Output* m_output;
            };

            std::map<uint64_t, uint8_t> backing_;

            c_MetaCache* metacache;
            c_2LvPredictor* cmpSize_predictor;

            uint64_t m_backing_size;
            bool loopback_en;
            bool compression_en;
            bool pca_mode;
            bool oracle_mode;
            int verbosity;

            int m_total_num_banks= m_deviceDriver->getTotalNumBank();
            int m_chnum=m_deviceDriver->getNumChannel();
            int m_ranknum=m_deviceDriver->getNumRanksPerChannel();
            int m_bgnum=m_deviceDriver->getNumBankGroupsPerRank();
            int m_banknum=m_deviceDriver->getNumBanksPerBankGroup();
            int m_rownum=m_deviceDriver->getNumRowsPerBank();
            int m_colnum=m_deviceDriver->getNumColPerBank();

            c_CompressEngine* m_compEngine;
            SST::Link *m_contentLink;
            int metadata_predictor;

            // Statistics
            std::map<int, uint64_t> m_normalized_size;
            //std::vector<std::vector<c_RowStat*>> m_row_stat;
            std::map<uint32_t, std::map<uint32_t, c_RowStat*>> m_row_stat;
            void handleContentEvent(SST::Event *ev);

            Statistic<double>* s_CompRatio;
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




        };
    }
}


#endif //C_CONTROLLER_HPP
