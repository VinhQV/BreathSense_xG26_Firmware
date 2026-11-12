/***************************************************************************//**
 * @file breathsense_time_sync.c
 * @brief Unix-time synchronization using a 64-bit monotonic sleeptimer tick.
 ******************************************************************************/

#include "breathsense_time_sync.h"

#include <limits.h>

#include "em_core.h"
#include "sl_sleeptimer.h"

static uint32_t synchronized_epoch_s;
static uint64_t synchronized_tick;
static bool synchronized_time_valid;

void breathsense_time_sync_init(void)
{
  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  synchronized_epoch_s = 0u;
  synchronized_tick = 0u;
  synchronized_time_valid = false;
  CORE_EXIT_ATOMIC();
}

bool breathsense_time_sync_set_epoch(uint32_t epoch_s)
{
  if (epoch_s < BREATHSENSE_TIME_MIN_EPOCH
      || epoch_s > BREATHSENSE_TIME_MAX_EPOCH) {
    return false; /* Never replace a good time base with an unset Pi clock. */
  }
  const uint64_t current_tick = sl_sleeptimer_get_tick_count64();

  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  synchronized_epoch_s = epoch_s;
  synchronized_tick = current_tick;
  synchronized_time_valid = true;
  CORE_EXIT_ATOMIC();
  return true;
}

bool breathsense_time_sync_get_epoch(uint32_t *epoch_s)
{
  if (epoch_s == NULL) {
    return false;
  }

  uint32_t base_epoch_s;
  uint64_t base_tick;
  bool valid;

  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  base_epoch_s = synchronized_epoch_s;
  base_tick = synchronized_tick;
  valid = synchronized_time_valid;
  CORE_EXIT_ATOMIC();

  if (!valid) {
    *epoch_s = 0u;
    return false;
  }

  const uint64_t current_tick = sl_sleeptimer_get_tick_count64();
  const uint32_t timer_frequency = sl_sleeptimer_get_timer_frequency();

  if (timer_frequency == 0u) {
    *epoch_s = 0u;
    return false;
  }

  const uint64_t elapsed_seconds =
    (current_tick - base_tick) / timer_frequency;

  if (elapsed_seconds > (uint64_t)(UINT32_MAX - base_epoch_s)) {
    *epoch_s = UINT32_MAX;
  } else {
    *epoch_s = base_epoch_s + (uint32_t)elapsed_seconds;
  }

  return true;
}

bool breathsense_time_sync_is_valid(void)
{
  bool valid;

  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();
  valid = synchronized_time_valid;
  CORE_EXIT_ATOMIC();

  return valid;
}
