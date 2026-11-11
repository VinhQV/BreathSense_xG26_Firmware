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

#define BREATHSENSE_AI_STATUS_SIZE 10u
/* Wire states: 0 starting, 1 capturing, 2 paused, 3 fault. */
void breathsense_ai_status(uint8_t data[BREATHSENSE_AI_STATUS_SIZE]);
void breathsense_ai_init(void);
/* Request a capture change. The AI task owns the microphone lifecycle. */
void breathsense_ai_set_capture_paused(bool paused);

bool breathsense_ai_get_latest(
  breathsense_ai_result_t *result
);

#ifdef __cplusplus
}
#endif

#endif  // BREATHSENSE_AI_H
