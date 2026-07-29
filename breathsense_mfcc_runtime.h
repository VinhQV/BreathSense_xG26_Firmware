#ifndef BREATHSENSE_MFCC_RUNTIME_H
#define BREATHSENSE_MFCC_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "breathsense_mfcc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

bool breathsense_mfcc_runtime_init(void);

/**
 * Float32 reference/runtime path.
 */
bool breathsense_mfcc_runtime_process_f32(
  const float *pcm,
  size_t pcm_sample_count,
  int8_t *output_int8,
  size_t output_count);

/**
 * Live microphone path.
 *
 * PCM16 samples are converted internally with:
 *   pcm_float = pcm16 / 32768.0f
 *
 * This avoids allocating a separate 25,600-element float buffer.
 */
bool breathsense_mfcc_runtime_process_pcm16(
  const int16_t *pcm,
  size_t pcm_sample_count,
  int8_t *output_int8,
  size_t output_count);

#ifdef __cplusplus
}
#endif

#endif
