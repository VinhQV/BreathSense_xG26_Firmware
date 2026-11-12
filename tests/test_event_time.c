#include <assert.h>
#include <stdio.h>
#include "breathsense_time_sync.h"
#include "breathsense_event.h"
uint64_t test_ticks;

int main(void)
{
  uint32_t now = 123;
  breathsense_event_t event;
  breathsense_time_sync_init();
  breathsense_event_init();
  assert(!breathsense_time_sync_get_epoch(&now) && now == 0);
  assert(!breathsense_time_sync_set_epoch(0));
  assert(!breathsense_time_sync_set_epoch(UINT32_MAX));
  /* Events before first sync remain unknown, never assigned connection time. */
  assert(breathsense_event_push_cough(123,false,BREATHSENSE_COUGH_TYPE_UNKNOWN,90,0,1000,false));
  assert(breathsense_time_sync_set_epoch(1794452400u)); /* 10:00 local. */
  assert(breathsense_event_peek(&event) && event.timestamp_s == 0 && !(event.flags & 1));
  breathsense_event_pop();
  assert(breathsense_time_sync_get_epoch(&now) && now == 1794452400u);
  assert(breathsense_event_push_cough(now,true,BREATHSENSE_COUGH_TYPE_DRY,90,80,2000,false));
  test_ticks += (uint64_t)300 * 32768u;
  assert(breathsense_time_sync_get_epoch(&now) && now == 1794452700u);
  /* Reconnection installs 10:05; the queued 10:00 cough is immutable. */
  assert(breathsense_time_sync_set_epoch(now));
  assert(breathsense_event_peek(&event) && event.timestamp_s == 1794452400u);
  assert(event.flags & BREATHSENSE_EVENT_FLAG_TIMESTAMP_VALID);
  /* Clock corrections and bad Pi values cannot rewrite an existing event. */
  assert(breathsense_time_sync_set_epoch(1794452600u));
  assert(!breathsense_time_sync_set_epoch(1));
  assert(breathsense_event_peek(&event) && event.timestamp_s == 1794452400u);
  test_ticks += (uint64_t)UINT32_MAX + 1u;
  assert(breathsense_time_sync_get_epoch(&now) && now == 1794452600u + 131072u);
  breathsense_time_sync_init();
  assert(!breathsense_time_sync_get_epoch(&now) && now == 0);
  puts("PASS: 10:00 cough delivered at 10:05, resync, invalid clock, pre-sync unknown, tick wrap, reset");
  return 0;
}
