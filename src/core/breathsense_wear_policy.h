#ifndef BREATHSENSE_WEAR_POLICY_H
#define BREATHSENSE_WEAR_POLICY_H
#include <stdbool.h>
#include <stdint.h>

/* One window = 50 samples at 25 Hz. UNKNOWN never changes power state. */
typedef struct {
  uint8_t wear, candidate, candidate_windows, verification_windows;
  bool active;
} breathsense_wear_policy_t;

static inline void breathsense_wear_policy_wake(breathsense_wear_policy_t *p)
{
  p->candidate = 0u;
  p->candidate_windows = 0u;
  p->verification_windows = 0u;
  p->wear = 0u;
}

static inline bool breathsense_wear_policy_update(breathsense_wear_policy_t *p,
                                                 bool valid, uint8_t motion,
                                                 uint8_t flat)
{
  uint8_t proposed = 0u;
  if (valid && motion >= 6u && flat < 25u) { proposed = 1u; }
  else if (valid && motion <= 1u && flat >= 48u) { proposed = 2u; }
  if (!valid) {
    p->wear = p->candidate = p->candidate_windows = 0u;
  } else {
    if (proposed != p->candidate) { p->candidate = proposed; p->candidate_windows = 0u; }
    uint8_t required = proposed == 2u ? 4u : 2u;
    if (p->candidate_windows < required && ++p->candidate_windows == required) {
      p->wear = proposed;
      if (proposed == 1u) { p->active = true; }
      if (proposed == 2u) { p->active = false; }
    }
  }
  if (p->active) { p->verification_windows = 0u; return false; }
  if (p->verification_windows < 5u) { ++p->verification_windows; }
  /* End the bounded verification even if light motion stays UNKNOWN. */
  return p->wear == 2u || p->verification_windows >= 5u;
}
#endif
