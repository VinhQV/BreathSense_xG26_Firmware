#ifndef BREATHSENSE_STAGE2_LOGMEL_COEFFICIENTS_H
#define BREATHSENSE_STAGE2_LOGMEL_COEFFICIENTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BREATHSENSE_STAGE2_FFT_SIZE            1024u
#define BREATHSENSE_STAGE2_SPECTRUM_BIN_COUNT  513u
#define BREATHSENSE_STAGE2_MEL_COUNT           40u
#define BREATHSENSE_STAGE2_FRAME_COUNT         50u
#define BREATHSENSE_STAGE2_MEL_WEIGHT_COUNT    981u

extern const float
  g_breathsense_stage2_hann[BREATHSENSE_STAGE2_FFT_SIZE];

extern const uint16_t
  g_breathsense_stage2_mel_start[BREATHSENSE_STAGE2_MEL_COUNT];

extern const uint16_t
  g_breathsense_stage2_mel_length[BREATHSENSE_STAGE2_MEL_COUNT];

extern const uint16_t
  g_breathsense_stage2_mel_offset[BREATHSENSE_STAGE2_MEL_COUNT];

extern const float
  g_breathsense_stage2_mel_weights[
    BREATHSENSE_STAGE2_MEL_WEIGHT_COUNT
  ];

#ifdef __cplusplus
}
#endif

#endif  // BREATHSENSE_STAGE2_LOGMEL_COEFFICIENTS_H
