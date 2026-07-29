#ifndef BREATHSENSE_INFERENCE_RUNTIME_H
#define BREATHSENSE_INFERENCE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BREATHSENSE_MODEL_INPUT_COUNT   2000u
#define BREATHSENSE_MODEL_OUTPUT_COUNT  4u

/**
 * Initialize the independent BreathSense TFLite Micro interpreter.
 *
 * This interpreter uses the model in breathsense_model_data.cc and does not
 * depend on the Silicon Labs Voice Control Light interpreter.
 */
bool breathsense_inference_runtime_init(void);

/**
 * Run one inference.
 *
 * input_int8:
 *   Flattened model tensor [1, 40, 50, 1].
 *
 * output_int8:
 *   Four quantized Softmax outputs in this fixed class order:
 *     0 = cough
 *     1 = other
 *     2 = sneeze
 *     3 = speech
 *
 * probabilities:
 *   Optional float probabilities. Pass NULL when they are not needed.
 *
 * This function is not re-entrant. Call it from only one AI task.
 */
bool breathsense_inference_runtime_run(
  const int8_t *input_int8,
  size_t input_count,
  int8_t output_int8[BREATHSENSE_MODEL_OUTPUT_COUNT],
  float probabilities[BREATHSENSE_MODEL_OUTPUT_COUNT]);

#ifdef __cplusplus
}
#endif

#endif
