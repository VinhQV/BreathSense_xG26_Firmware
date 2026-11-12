#include <stdint.h>
extern uint64_t test_ticks;
static inline uint64_t sl_sleeptimer_get_tick_count64(void) { return test_ticks; }
static inline uint32_t sl_sleeptimer_get_timer_frequency(void) { return 32768u; }
