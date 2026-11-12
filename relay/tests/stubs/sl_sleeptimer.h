typedef struct { int unused; } sl_sleeptimer_timer_handle_t;
#define sl_sleeptimer_get_tick_count64() ((uint64_t)test_time)
#define sl_sleeptimer_tick64_to_ms(ticks, out) (*(out) = (ticks), SL_STATUS_OK)
#define sl_sleeptimer_start_periodic_timer_ms(...) SL_STATUS_OK
