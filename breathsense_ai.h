/***************************************************************************//**
 * @file breathsense_ai.h
 * @brief Stable AI interface used by BreathSense application and PAwR payload.
 ******************************************************************************/

#ifndef BREATHSENSE_AI_H
#define BREATHSENSE_AI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  BREATHSENSE_AI_COUGH  = 0,
  BREATHSENSE_AI_OTHER  = 1,
  BREATHSENSE_AI_SNEEZE = 2,
  BREATHSENSE_AI_SPEECH = 3
} breathsense_ai_class_t;

typedef struct {
  breathsense_ai_class_t class_id;
  uint8_t confidence_percent;
  uint16_t sequence;
  uint32_t timestamp_ms;
  bool valid;
} breathsense_ai_result_t;

void breathsense_ai_init(void);

bool breathsense_ai_get_latest(
  breathsense_ai_result_t *result
);

#ifdef __cplusplus
}
#endif

#endif  // BREATHSENSE_AI_H
