
#ifndef CRAMSIM_TYPES_HPP_H
#define CRAMSIM_TYPES_HPP_H

namespace SST{
    namespace n_Bank{
        enum class e_BankTiming{
            nRC, nRRD, nRRD_L, nRRD_S, nRCD, nCCD, nCCD_L, nCCD_L_WR, nCCD_S, nAL, nCL, nCWL, nWR, nWTR, nWTR_L, nWTR_S, nRTW, nEWTR, nEWTW,
            nERTR,nERTW, nRAS, nRTP, nRP, nRFC, nREFI, nFAW, nBL
        };
    }
}

#endif //CRAMSIM_TYPES_HPP_H
