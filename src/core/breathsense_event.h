/***************************************************************************//**
 * @file breathsense_event.h
 * @brief BreathSense immutable cough-event queue.
 ******************************************************************************/

#ifndef BREATHSENSE_EVENT_H
#define BREATHSENSE_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 ******************************* CONFIGURATION *********************************
 ******************************************************************************/

/*
 * Eight events are sufficient for offline validation and the first PAwR demo.
 *
 * One event occupies approximately 12 bytes, therefore this queue consumes
 * approximately 96 bytes of static RAM.
 */
#define BREATHSENSE_EVENT_QUEUE_DEPTH  8u

/*******************************************************************************
 ******************************** EVENT TYPES **********************************
 ******************************************************************************/

typedef enum {
  BREATHSENSE_COUGH_TYPE_UNKNOWN = 0u,
  BREATHSENSE_COUGH_TYPE_DRY     = 1u,
  BREATHSENSE_COUGH_TYPE_WET     = 2u
} breathsense_cough_type_t;

/*
 * Event flag layout:
 *
 * bit 0: timestamp is synchronized Unix time
 * bit 1: Stage 2 result is valid
 * bit 2..7: reserved
 */
#define BREATHSENSE_EVENT_FLAG_TIMESTAMP_VALID  (1u << 0)
#define BREATHSENSE_EVENT_FLAG_STAGE2_VALID     (1u << 1)

typedef struct {
  uint32_t timestamp_s;
  uint16_t event_counter;

  uint8_t cough_type;
  uint8_t stage1_confidence;
  uint8_t stage2_confidence;
  uint8_t flags;
} breathsense_event_t;

/*******************************************************************************
 ******************************* PUBLIC FUNCTIONS *******************************
 ******************************************************************************/

/**
 * Initialize the event queue.
 *
 * Call once from app_init() before starting the AI task.
 */
void breathsense_event_init(void);

/**
 * Create and enqueue one immutable cough event.
 *
 * The event counter is assigned internally only when the event is successfully
 * inserted into the queue.
 *
 * @param timestamp_s          Unix timestamp or local uptime in seconds.
 * @param timestamp_valid      True when timestamp_s is synchronized Unix time.
 * @param cough_type           Unknown, dry or wet.
 * @param stage1_confidence    Stage 1 confidence from 0 to 100.
 * @param stage2_confidence    Stage 2 confidence from 0 to 100.
 *
 * @return true if queued successfully.
 * @return false if the queue is full or the arguments are invalid.
 */
bool breathsense_event_push_cough(
  uint32_t timestamp_s,
  bool timestamp_valid,
  breathsense_cough_type_t cough_type,
  uint8_t stage1_confidence,
  uint8_t stage2_confidence);

/**
 * Copy the oldest queued event without removing it.
 */
bool breathsense_event_peek(breathsense_event_t *event);

/**
 * Remove the oldest queued event.
 *
 * This should only be called after the transport has accepted the packet.
 */
bool breathsense_event_pop(void);

/**
 * Return the number of events currently waiting in the queue.
 */
uint8_t breathsense_event_count(void);

/**
 * Return the number of events rejected because the queue was full.
 */
uint32_t breathsense_event_dropped_count(void);

/**
 * Return the counter that will be assigned to the next successful event.
 */
uint16_t breathsense_event_next_counter(void);

#ifdef __cplusplus
}
#endif

#endif /* BREATHSENSE_EVENT_H */