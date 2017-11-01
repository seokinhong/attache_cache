//
// Created by seokin on 10/31/17.
//

#include "c_CompressEngine.hpp"
#include <map>
#include "stdlib.h"
#include "stdio.h"
#include "string.h"


using namespace SST;

c_CompressEngine::c_CompressEngine(int verbosity, bool valid_en=false)
{
    // internal params
    output = new SST::Output("[Compressor, @f:@l:@p] ",
                             verbosity, 0, SST::Output::STDOUT);
    ;
    validation_en=valid_en;

}
c_CompressEngine::~c_CompressEngine()
{
    ;
}

uint32_t c_CompressEngine::getCompressedSize(uint8_t *cacheline, COMP_ALG comp_alg)
{

    int min_compressed_size = 512;

    if(comp_alg == COMP_ALG::BDI) {

        std::vector<uint64_t> data_vec;

        std::vector<uint32_t> base_size(3);
        std::map<uint32_t, uint32_t> min_delta_size_map;
        std::map<uint32_t, uint64_t> min_base_map;
        std::map<uint32_t, uint32_t> min_compressed_size_map;
        base_size = {2, 4, 8}; //

        std::vector<uint64_t> delta_base_vector;
        std::vector<uint64_t> delta_immd_vector;
        std::vector<uint64_t> delta_flag;
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

            uint64_t delta_base = 0;
            uint64_t delta_immd = 0;
            uint64_t immd = 0;

            //calculate the delta
            int compressed_size=0;

            for (auto &base:data_vec) {
                delta_immd_vector.clear();
                delta_base_vector.clear();
                delta_flag.clear();
                base_id=0;
                for (auto &data:data_vec) {
                    delta_base = base - data;
                    delta_immd = data - immd;

                    if (abs(delta_base) >= abs(delta_immd)) {
                        delta_immd_vector.push_back(delta_immd);
                        delta_flag.push_back(0);
                    }
                    else {
                        delta_base_vector.push_back(delta_base);
                        delta_flag.push_back(1);
                    }
                }

                int max_delta_size_immd = 0;
                int max_delta_size_base = 0;
                for (auto &delta:delta_immd_vector) {

                    int size = getDataSize(delta);
                    if (max_delta_size_immd < size)
                        max_delta_size_immd = size;
                }

                for (auto &delta:delta_base_vector) {
                    int size = getDataSize(delta);
                    if (max_delta_size_base < size)
                        max_delta_size_base = size;
                }

                compressed_size =
                        k * 8 + delta_base_vector.size() * max_delta_size_base +
                        delta_immd_vector.size() * max_delta_size_immd +
                        (64 / k) + 10*2 + 2; //bits (base+ delta@base + delta@immd + delta_flag + delta_size + base_size)

                output->verbose(CALL_INFO,2,0,"k:%d base: %llx max_delta_size_base: %d max_delta_size_immd: %d num_delta_base:%d num_delta_immd:%d compressed_size:%d compression ratio:%lf\n",
                       k, base, max_delta_size_base, max_delta_size_immd, delta_base_vector.size(),
                       delta_immd_vector.size(), compressed_size, (double) 512 / (double) compressed_size);

                if (compressed_size < min_compressed_size) {
                    min_k = k;
                    min_base = base;
                    min_compressed_size = compressed_size;

                    //validate compression algorithm
                    if(validation_en==true) {
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
                                data = base - delta;
                            }
                            decompressed_data.push_back(data);
                            output->verbose(CALL_INFO,2,0,"decompressed data: %llx\n", data);
                            if (data != data_vec[data_idx++]) {
                                printf("decompression error\n");
                                exit(1);
                            }

                        }
                    }
                }
            }
        }
        output->verbose(CALL_INFO,1,0,"[CompressionResult] k:%d base: %llx compressed_size:%d compression ratio:%lf\n",
                   min_k, min_base, min_compressed_size, (double) 512 / (double) min_compressed_size);
    }

    return min_compressed_size;
}



uint32_t c_CompressEngine::getDataSize(uint64_t data_)
{
    int size=0; //bit

    uint64_t data=abs(data_);
    for(int i=0; i<64; i++)
    {
        if(data==(data & ~(0xffffffffffffffff<<i)))
        {
            size = i+1; //1 bit is used for sign bit.
            break;
        }
    }

    return size;
}


/*
uint32_t c_CompressEngine::getCompressedSize(uint8_t *cacheline, COMP_ALG comp_alg)
{

    if(comp_alg == COMP_ALG::BDI)
    {

        std::vector<uint64_t> data_vec;

        std::vector<uint64_t> delta;

        std::vector<uint32_t> base_size(3);
        std::vector<uint32_t> delta_size(7);
        std::map<uint32_t,uint32_t> min_delta_size_map;
        std::map<uint32_t,uint64_t> min_base_map;
        std::map<uint32_t,uint32_t> min_compressed_size_map;
        base_size = {2,4,8}; //
        delta_size = {1,2,3,4,5,6,7};


        for(auto &k: base_size) {
            uint8_t *ptr;
            data_vec.clear();
            for(int i=0;i<64/k;i++) {
                ptr=(cacheline+i*k);
                if(k==2)
                    data_vec.push_back((uint64_t)(*(uint16_t*)ptr));
                else if(k==4)
                    data_vec.push_back((uint64_t)(*(uint32_t*)ptr));
                else if(k==8)
                    data_vec.push_back((uint64_t)(*(uint64_t*)ptr));
            }

            int min_delta_size=k;
            uint64_t min_base=0;
            int min_compressed_size=64;
            for (auto &d: delta_size)
            {
                if(d>=k)
                    continue;
                else
                {
                    for(auto &base:data_vec) {
                        int cnt=0;

                        //calculate delta
                        for (auto &data:data_vec) {
                            int64_t delta = base - data;
                            delta = (delta << (8 - d) * 8) >> (8 - d) * 8;
                            if (data + delta != base) {
                                printf("[Fail] delta_size:%d base_size:%d base:%llx data:%llx delta:%llx\n",d,k,base,data,delta);
                                break;
                            }
                            else {
                                printf("[success] delta_size:%d base_size:%d base:%llx data:%llx delta:%llx\n",d,k,base,data,delta);
                                cnt++;
                            }
                        }

                        //calculate the mininum compressed size
                        if (cnt == data_vec.size())
                        {
                            int compressedsize=k+d*(64/k);
                            if(compressedsize<min_compressed_size) {
                                min_compressed_size = compressedsize;
                                min_base = base;
                                min_delta_size = d;
                            }
                        }
                    }
                }
                if(min_delta_size!=k)
                    break;
            }
            min_base_map[k]=min_base;
            min_delta_size_map[k]=min_delta_size;
            min_compressed_size_map[k]=min_compressed_size;
            printf("k:%d min_delta_size: %d min_delta_base:%d compressedsize:%d\n\n",k,min_delta_size, min_base,min_compressed_size);
        }
        int min_compressed_size=64;
        int min_k=0;
        int min_delta=0;
        for(auto &k:base_size)
        {
            printf("[compResult] k:%d compressionSize:%d compressionRatio:%lf base:%lld delta_size:%d\n\n",
                   k,min_compressed_size_map[k],(double)64/(double)min_compressed_size_map[k],min_base_map[k],min_delta_size_map[k]);
            if(min_compressed_size_map[k]<min_compressed_size)
            {
                min_compressed_size=min_compressed_size_map[k];
                min_k=k;
                min_delta=min_delta_size_map[k];
            }
        }

    }
}
 */