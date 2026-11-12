#ifndef BREATHSENSE_RELAY_CONFIG_H
#define BREATHSENSE_RELAY_CONFIG_H

/* One dedicated BreathSense node -> one relay -> one Pi gateway.
 * Set the exact source address before use with several identically named nodes.
 * Address text has the usual display order, e.g. "AA:BB:CC:DD:EE:FF".
 * An empty address selects the exact name and pins that address until reboot. */
#define RELAY_SOURCE_ADDRESS "C0:2C:ED:90:AD:BA"
#define RELAY_SOURCE_NAME "MyDevice_01"
#define RELAY_NAME "BSRelay_01"
#define RELAY_QUEUE_CAPACITY 64u
#define RELAY_TICK_MS 100u
#define RELAY_RETRY_MS 3000u
#define RELAY_PROCEDURE_TIMEOUT_MS 15000u
#define RELAY_POLL_MS 2000u
#define RELAY_CACHE_MAX_AGE_MS 10000u

#endif
