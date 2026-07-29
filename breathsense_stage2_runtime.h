#ifndef BREATHSENSE_STAGE2_RUNTIME_H
#define BREATHSENSE_STAGE2_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BREATHSENSE_STAGE2_INPUT_COUNT       2000u
#define BREATHSENSE_STAGE2_OUTPUT_COUNT      2u
#define BREATHSENSE_STAGE2_MARGIN_THRESHOLD  15u

typedef enum {
  BREATHSENSE_STAGE2_DRY = 0,
  BREATHSENSE_STAGE2_WET,
  BREATHSENSE_STAGE2_REJECTED,
  BREATHSENSE_STAGE2_ERROR
} breathsense_stage2_decision_t;

typedef struct {
  int8_t dry_score;
  int8_t wet_score;
  uint8_t margin;
  breathsense_stage2_decision_t decision;
  bool valid;
} breathsense_stage2_result_t;

/**
 * Initialize the independent Stage 2 TFLite Micro interpreter.
 *
 * Expected model:
 *   Input  INT8 [1, 40, 50, 1]
 *   Output INT8 [1, 2]
 *   Class  0 = dry, 1 = wet
 */
bool breathsense_stage2_runtime_init(void);

/**
 * Run Stage 2 on one already-prepared Log-Mel INT8 tensor.
 *
 * This function is not re-entrant. It must be called from one task only.
 */
bool breathsense_stage2_runtime_run(
  const int8_t *input_int8,
  size_t input_count,
  breathsense_stage2_result_t *result);

#ifdef __cplusplus
}
#endif

#endif  // BREATHSENSE_STAGE2_RUNTIME_H
