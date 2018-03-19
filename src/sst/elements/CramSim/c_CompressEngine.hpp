//
// Created by seokin on 10/31/17.
//

#ifndef C_COMPRESSENGINE_HPP
#define C_COMPRESSENGINE_HPP
#include <sst/core/sst_types.h>

#include <string>
#include <vector>
#include <sst/core/output.h>

namespace SST {

    typedef enum comp_alg_ {
        FPC,
        BDI,
        FVC,
        COMP_ALG_NUM,
    } COMP_ALG;

    class c_CompressEngine {

    public:
        c_CompressEngine();
        c_CompressEngine(int verbose, bool validation_en);
        c_CompressEngine(uint32_t compressed_data_ratio);

        ~c_CompressEngine();

        uint32_t getCompressedSize(uint8_t *cacheline, COMP_ALG comp_alg);
        uint32_t getCompressedSize();
        uint32_t getDataSize(int64_t data);
        int64_t getSignedExtension(int64_t data, uint32_t size);


        private:
        //Debug
        Output *output;
        bool validation_en;
        uint32_t m_compressed_data_ratio;
        bool m_fixed_compression_mode;
    };
}

#endif //SRC_PCA_C_COMPRESSENGINE_HPP
