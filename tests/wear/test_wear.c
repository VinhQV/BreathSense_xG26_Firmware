#include <assert.h>
#include <stdio.h>
#include "breathsense_wear_policy.h"
int main(void)
{
  breathsense_wear_policy_t p = {0};
  /* A stationary boot must not start applications. OFF_BODY takes 8 seconds. */
  for (unsigned i=0; i<3; ++i) { assert(!breathsense_wear_policy_update(&p,true,0,50)); assert(!p.active); }
  assert(breathsense_wear_policy_update(&p,true,0,50));
  assert(p.wear == 2 && !p.active);
  /* Wake IRQ/light handling alone may consume at most 10 seconds of IMU verification. */
  breathsense_wear_policy_wake(&p);
  for (unsigned i=0; i<4; ++i) { assert(!breathsense_wear_policy_update(&p,true,3,15)); assert(!p.active); }
  assert(breathsense_wear_policy_update(&p,true,3,15)); assert(!p.active && p.wear==0);
  /* Brief strong movement is insufficient; repeated non-flat activity wakes. */
  breathsense_wear_policy_wake(&p);
  assert(!breathsense_wear_policy_update(&p,true,20,0)); assert(!p.active);
  assert(!breathsense_wear_policy_update(&p,true,20,0)); assert(p.active && p.wear==1);
  /* Seated/still wearer is UNKNOWN and keeps previous confirmed active mode. */
  for (unsigned i=0; i<20; ++i) { assert(!breathsense_wear_policy_update(&p,true,0,0)); assert(p.active); }
  assert(p.wear==0);
  /* Do not shut down on one brief flat gesture. */
  for (unsigned i=0; i<3; ++i) { assert(!breathsense_wear_policy_update(&p,true,0,50)); assert(p.active); }
  assert(breathsense_wear_policy_update(&p,true,0,50)); assert(!p.active);
  /* A shaking table cannot activate capture, even at high motion. */
  breathsense_wear_policy_wake(&p);
  for (unsigned i=0; i<10; ++i) { breathsense_wear_policy_update(&p,true,50,50); assert(!p.active); }
  /* Invalid IMU samples must never activate an off-body node. */
  breathsense_wear_policy_wake(&p);
  for (unsigned i=0; i<5; ++i) { breathsense_wear_policy_update(&p,false,50,0); assert(!p.active); }
  puts("PASS: boot off, hysteresis, UNKNOWN latch, bounded verification, sensor errors");
}
