//
// Created by seokin on 10/31/17.
//

#include "c_CompressEngine.hpp"
#include <map>
#include <math.h>
#include <stdlib.h>
#include "stdio.h"
#include "string.h"
#include "assert.h"


using namespace SST;

c_CompressEngine::c_CompressEngine(int verbosity, bool valid_en=false)
{
    // internal params
    output = new SST::Output("[Compressor, @f:@l:@p] ",
                             verbosity, 0, SST::Output::STDOUT);
    validation_en=valid_en;
    m_fixed_compression_mode=false;
}

c_CompressEngine::c_CompressEngine(uint32_t compressed_data_ratio){
    m_compressed_data_ratio=compressed_data_ratio;
    m_fixed_compression_mode=true;
}



uint32_t c_CompressEngine::getCompressedSize()
{
    uint64_t value = rand() % 100;

    if (value < m_compressed_data_ratio)
        return 50;
    else
        return 100;
}
uint32_t c_CompressEngine::getCompressedSize(uint8_t *cacheline, COMP_ALG comp_alg)
{


        int min_compressed_size = 512;

        if (comp_alg == COMP_ALG::BDI) {

            std::vector<uint64_t> data_vec;

            std::vector<uint32_t> base_size(3);
            std::map<uint32_t, uint32_t> min_delta_size_map;
            std::map<uint32_t, uint64_t> min_base_map;
            std::map<uint32_t, uint32_t> min_compressed_size_map;
            //base_size = {2, 4, 8}; //
            base_size = {4, 8}; //
            //base_size = {8}; //

            std::vector<int64_t> delta_base_vector;
            std::vector<int64_t> delta_immd_vector;
            std::vector<uint32_t> delta_flag;
            uint32_t base_id;

            uint64_t min_base = 0;
            int min_k = 0;


            for (auto &k: base_size) {
                uint8_t *ptr;
                data_vec.clear();
                for (int i = 0; i < 64 / k; i++) {
                    ptr = (cacheline + i * k);
                    if (k == 2)
                        data_vec.push_back((uint64_t) (*(uint16_t *) ptr));
                    else if (k == 4)
                        data_vec.push_back((uint64_t) (*(uint32_t *) ptr));
                    else if (k == 8)
                        data_vec.push_back((uint64_t) (*(uint64_t *) ptr));
                }

                int64_t delta_base = 0;
                int64_t delta_immd = 0;
                int64_t immd = 0;

                //calculate the delta
                int compressed_size = 0;

                for (auto &base:data_vec) {
                    delta_immd_vector.clear();
                    delta_base_vector.clear();
                    delta_flag.clear();
                    base_id = 0;

                    int max_delta_size_immd = 0;
                    int max_delta_size_base = 0;

                    //calculate delta
                    for (auto &data:data_vec) {

                        delta_base = data - base;
                        delta_immd = data - immd;

                        if (llabs(delta_base) >= llabs(delta_immd)) {
                            int size = getDataSize(delta_immd);
                            if (size > 64)
                                break;

                            if (max_delta_size_immd < size)
                                max_delta_size_immd = size;

                            delta_immd_vector.push_back(delta_immd);
                            delta_flag.push_back(0);

                        } else {
                            int size = getDataSize(delta_base);
                            if (size > 64)
                                break;

                            if (max_delta_size_base < size)
                                max_delta_size_base = size;

                            delta_base_vector.push_back(delta_base);
                            delta_flag.push_back(1);
                        }
                    }

                    //calculate compressed size
                    compressed_size =
                            k * 8 + delta_base_vector.size() * max_delta_size_base +
                            delta_immd_vector.size() * max_delta_size_immd +
                            (64 / k) + 6 * 2 + 2; //bits
                    // compressed_size = base + delta@base + delta@immd
                    //                   + delta_flag       //indicate base
                    //                   + delta_size
                    //                   + base_size        // (2B or 4B or 8B)

                    output->verbose(CALL_INFO, 5, 0,
                                    "k:%d base: %llx max_delta_size_base: %d max_delta_size_immd: %d num_delta_base:%d num_delta_immd:%d compressed_size:%d compression ratio:%lf\n",
                                    k, base, max_delta_size_base, max_delta_size_immd, delta_base_vector.size(),
                                    delta_immd_vector.size(), compressed_size, (double) 512 / (double) compressed_size);


                    //get min compressed size
                    if (compressed_size < min_compressed_size) {
                        min_k = k;
                        min_base = base;
                        min_compressed_size = compressed_size;

                        //validate compression algorithm
                        if (validation_en == true) {
                            int base_idx = 0;
                            int immd_idx = 0;
                            int data_idx = 0;
                            uint64_t delta = 0;

                            std::vector<uint64_t> decompressed_data;

                            for (auto &flag:delta_flag) {
                                uint64_t data;
                                if (flag == 0) {
                                    delta = delta_immd_vector[immd_idx++];
                                    data = delta;
                                } else {
                                    delta = delta_base_vector[base_idx++];

                                    data = base + getSignedExtension(delta, getDataSize(delta));
                                }
                                decompressed_data.push_back(data);
                                output->verbose(CALL_INFO, 5, 0, "data: %llx decompressed data: %llx\n",
                                                data_vec[data_idx], data);
                                if (data != data_vec[data_idx++]) {
                                    printf("decompression error\n");
                                    exit(1);
                                }

                            }
                        }
                    }
                }
            }
            output->verbose(CALL_INFO, 5, 0,
                            "[CompressionResult] k:%d base: %llx compressed_size:%d compression ratio:%lf\n",
                            min_k, min_base, min_compressed_size, (double) 512 / (double) min_compressed_size);
        }

        return min_compressed_size;
}


int64_t c_CompressEngine::getSignedExtension(int64_t data, uint32_t size) {
    assert(size<=65);
    int64_t new_data;
    new_data=(data<<(64-size))>>(64-size);
    return new_data;
}

uint32_t c_CompressEngine::getDataSize(int64_t data_)
{
    int size=65; //bit

    if(data_==0)
        return 0;

    uint64_t data=llabs(data_);

    for(int i=1; i<64; i++)
    {
        if(data==(data & ~(0xffffffffffffffff<<i)))
        {
            size = i+1; //1 bit is used for sign bit.
            break;
        }
    }

    return size;
}
