#ifndef BREATHSENSE_MFCC_COEFFICIENTS_H
#define BREATHSENSE_MFCC_COEFFICIENTS_H

#include <stdint.h>
#include "breathsense_mfcc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BS_MEL_WEIGHT_COUNT 938u

extern const float g_bs_hann_window[BS_MFCC_FFT_SIZE];

extern const uint16_t g_bs_mel_start[BS_MFCC_MEL_COUNT];
extern const uint16_t g_bs_mel_length[BS_MFCC_MEL_COUNT];
extern const uint16_t g_bs_mel_offset[BS_MFCC_MEL_COUNT];
extern const float g_bs_mel_weights[BS_MEL_WEIGHT_COUNT];

extern const float
  g_bs_dct_basis[BS_MFCC_COEFFICIENT_COUNT * BS_MFCC_MEL_COUNT];

#ifdef __cplusplus
}
#endif

#endif
