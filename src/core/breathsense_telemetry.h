#ifndef BREATHSENSE_TELEMETRY_H
#define BREATHSENSE_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BREATHSENSE_PAYLOAD_SIZE      14u
#define BREATHSENSE_HISTORY_SIZE      16u

void breathsense_telemetry_init(void);

/*
 * Call exactly once for each Si7021 measurement attempt.
 *
 * This creates one immutable protocol-v1 frame per sensor_sequence.
 * Repeated PAwR POLLs may return the same frame until the next sensor sample.
 */
void breathsense_telemetry_update_sensor(int32_t temperature_mc,
                                         uint32_t humidity_x1000,
                                         bool sensor_valid);

bool breathsense_telemetry_copy_latest(
  uint8_t payload[BREATHSENSE_PAYLOAD_SIZE]);

bool breathsense_telemetry_copy_by_sensor_sequence(
  uint16_t sensor_sequence,
  uint8_t payload[BREATHSENSE_PAYLOAD_SIZE]);

uint16_t breathsense_telemetry_get_latest_sensor_sequence(void);

#ifdef __cplusplus
}
#endif

#endif  // BREATHSENSE_TELEMETRY_H
