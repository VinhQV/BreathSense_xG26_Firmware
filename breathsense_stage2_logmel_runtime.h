#ifndef BREATHSENSE_STAGE2_LOGMEL_RUNTIME_H
#define BREATHSENSE_STAGE2_LOGMEL_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BREATHSENSE_STAGE2_AUDIO_SAMPLE_RATE_HZ  16000u
#define BREATHSENSE_STAGE2_AUDIO_WINDOW_SAMPLES  25600u
#define BREATHSENSE_STAGE2_LOGMEL_ELEMENT_COUNT  2000u

/**
 * Initialize the independent Stage 2 CMSIS-DSP Log-Mel frontend.
 */
bool breathsense_stage2_logmel_runtime_init(void);

/**
 * Convert mono PCM16 audio into the Stage 2 INT8 tensor:
 *   [1, 40 mel bins, 50 time frames, 1 channel]
 *
 * Contract:
 *   n_fft=1024, win_length=1024, periodic Hann, hop=512
 *   center=true with reflection padding
 *   power spectrum
 *   Slaney Mel, htk=false, 40 bins, 0..8000 Hz
 *   power_to_db(ref=max, amin=1e-10, top_db=80)
 *   per-sample min/max normalization
 *   INT8 scale=1/255, zero point=-128
 */
bool breathsense_stage2_logmel_runtime_process_pcm16(
  const int16_t *pcm,
  size_t pcm_sample_count,
  int8_t *output_int8,
  size_t output_count);

#ifdef __cplusplus
}
#endif

#endif  // BREATHSENSE_STAGE2_LOGMEL_RUNTIME_H
