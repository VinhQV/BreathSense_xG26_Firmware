/***************************************************************************//**
 * @file breathsense_event.c
 * @brief BreathSense immutable cough-event queue implementation.
 ******************************************************************************/

#include "breathsense_event.h"

#include <stddef.h>
#include <string.h>

#include "em_core.h"

/*******************************************************************************
 ******************************* STATIC STORAGE ********************************
 ******************************************************************************/

static breathsense_event_t
  event_queue[BREATHSENSE_EVENT_QUEUE_DEPTH];

static uint8_t queue_head;
static uint8_t queue_tail;
static uint8_t queue_count;

static uint16_t next_event_counter;
static uint32_t dropped_event_count;

/*******************************************************************************
 ******************************* LOCAL FUNCTIONS *******************************
 ******************************************************************************/

static bool cough_type_is_valid(
  breathsense_cough_type_t cough_type)
{
  return cough_type == BREATHSENSE_COUGH_TYPE_UNKNOWN
         || cough_type == BREATHSENSE_COUGH_TYPE_DRY
         || cough_type == BREATHSENSE_COUGH_TYPE_WET;
}

/*******************************************************************************
 ******************************* PUBLIC FUNCTIONS ******************************
 ******************************************************************************/

void breathsense_event_init(void)
{
  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();

  memset(event_queue, 0, sizeof(event_queue));

  queue_head = 0u;
  queue_tail = 0u;
  queue_count = 0u;

  next_event_counter = 0u;
  dropped_event_count = 0u;

  CORE_EXIT_ATOMIC();
}

bool breathsense_event_push_cough(
  uint32_t timestamp_s,
  bool timestamp_valid,
  breathsense_cough_type_t cough_type,
  uint8_t stage1_confidence,
  uint8_t stage2_confidence)
{
  if (!cough_type_is_valid(cough_type)) {
    return false;
  }

  if (stage1_confidence > 100u
      || stage2_confidence > 100u) {
    return false;
  }

  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();

  if (queue_count >= BREATHSENSE_EVENT_QUEUE_DEPTH) {
    ++dropped_event_count;

    CORE_EXIT_ATOMIC();
    return false;
  }

  breathsense_event_t *event = &event_queue[queue_tail];

  event->timestamp_s = timestamp_s;
  event->event_counter = next_event_counter;
  event->cough_type = (uint8_t)cough_type;
  event->stage1_confidence = stage1_confidence;
  event->stage2_confidence = stage2_confidence;
  event->flags = 0u;

  if (timestamp_valid) {
    event->flags |= BREATHSENSE_EVENT_FLAG_TIMESTAMP_VALID;
  }

  if (cough_type == BREATHSENSE_COUGH_TYPE_DRY
      || cough_type == BREATHSENSE_COUGH_TYPE_WET) {
    event->flags |= BREATHSENSE_EVENT_FLAG_STAGE2_VALID;
  }

  ++next_event_counter;

  ++queue_tail;

  if (queue_tail >= BREATHSENSE_EVENT_QUEUE_DEPTH) {
    queue_tail = 0u;
  }

  ++queue_count;

  CORE_EXIT_ATOMIC();

  return true;
}

bool breathsense_event_peek(breathsense_event_t *event)
{
  if (event == NULL) {
    return false;
  }

  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();

  if (queue_count == 0u) {
    CORE_EXIT_ATOMIC();
    return false;
  }

  *event = event_queue[queue_head];

  CORE_EXIT_ATOMIC();

  return true;
}

bool breathsense_event_pop(void)
{
  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();

  if (queue_count == 0u) {
    CORE_EXIT_ATOMIC();
    return false;
  }

  memset(&event_queue[queue_head],
         0,
         sizeof(event_queue[queue_head]));

  ++queue_head;

  if (queue_head >= BREATHSENSE_EVENT_QUEUE_DEPTH) {
    queue_head = 0u;
  }

  --queue_count;

  CORE_EXIT_ATOMIC();

  return true;
}

uint8_t breathsense_event_count(void)
{
  uint8_t count;

  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  count = queue_count;
  CORE_EXIT_ATOMIC();

  return count;
}

uint32_t breathsense_event_dropped_count(void)
{
  uint32_t count;

  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  count = dropped_event_count;
  CORE_EXIT_ATOMIC();

  return count;
}

uint16_t breathsense_event_next_counter(void)
{
  uint16_t counter;

  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  counter = next_event_counter;
  CORE_EXIT_ATOMIC();

  return counter;
}