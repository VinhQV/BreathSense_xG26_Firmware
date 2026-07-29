/***************************************************************************//**
 * @file breathsense_telemetry.c
 * @brief Immutable 14-byte BreathSense telemetry cache and resend history.
 ******************************************************************************/

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "breathsense_ai.h"
#include "breathsense_telemetry.h"
#include "breathsense_protocol_config.h"
#include "em_core.h"

#define BS_PAYLOAD_MAGIC              0xB5u
#define BS_PAYLOAD_VERSION            1u
#define BS_FLAG_SENSOR_VALID          (1u << 0)
#define BS_FLAG_AI_VALID              (1u << 1)

_Static_assert(BREATHSENSE_PAYLOAD_SIZE == 14u,
               "BreathSense payload contract must remain 14 bytes");

static uint8_t latest_payload[BREATHSENSE_PAYLOAD_SIZE];
static bool latest_payload_ready = false;

static uint8_t history[BREATHSENSE_HISTORY_SIZE]
                      [BREATHSENSE_PAYLOAD_SIZE];
static uint8_t history_write_index = 0u;
static uint8_t history_count = 0u;

static uint16_t sensor_sequence = 0u;

static void put_u16_le(uint8_t *destination, uint16_t value)
{
  destination[0] = (uint8_t)(value & 0xFFu);
  destination[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint16_t get_u16_le(const uint8_t *source)
{
  return (uint16_t)source[0]
         | ((uint16_t)source[1] << 8);
}

static int16_t temperature_mc_to_centi_c(int32_t temperature_mc)
{
  int32_t centi_c = temperature_mc / 10;

  if (centi_c > INT16_MAX) {
    centi_c = INT16_MAX;
  } else if (centi_c < INT16_MIN) {
    centi_c = INT16_MIN;
  }

  return (int16_t)centi_c;
}

static uint16_t humidity_x1000_to_centi_percent(uint32_t humidity_x1000)
{
  uint32_t centi_percent = humidity_x1000 / 10u;

  if (centi_percent > UINT16_MAX) {
    centi_percent = UINT16_MAX;
  }

  return (uint16_t)centi_percent;
}

static void build_payload(uint8_t payload[BREATHSENSE_PAYLOAD_SIZE],
                          uint16_t sequence,
                          int32_t temperature_mc,
                          uint32_t humidity_x1000,
                          bool sensor_valid,
                          const breathsense_ai_result_t *ai)
{
  const bool ai_valid =
    (ai != NULL)
    && ai->valid
    && ((uint8_t)ai->class_id <= (uint8_t)BREATHSENSE_AI_SPEECH);

  const int16_t temperature_centi_c =
    sensor_valid ? temperature_mc_to_centi_c(temperature_mc) : 0;

  const uint16_t humidity_centi_percent =
    sensor_valid
    ? humidity_x1000_to_centi_percent(humidity_x1000)
    : 0u;

  memset(payload, 0, BREATHSENSE_PAYLOAD_SIZE);

  payload[0] = BS_PAYLOAD_MAGIC;
  payload[1] = BS_PAYLOAD_VERSION;
  payload[2] = BREATHSENSE_NODE_ID;

  if (sensor_valid) {
    payload[3] |= BS_FLAG_SENSOR_VALID;
  }

  if (ai_valid) {
    payload[3] |= BS_FLAG_AI_VALID;
  }

  put_u16_le(&payload[4], sequence);
  put_u16_le(&payload[6], (uint16_t)temperature_centi_c);
  put_u16_le(&payload[8], humidity_centi_percent);

  payload[10] = ai_valid
                ? (uint8_t)ai->class_id
                : (uint8_t)BREATHSENSE_AI_OTHER;

  payload[11] = ai_valid ? ai->confidence_percent : 0u;

  put_u16_le(&payload[12], ai_valid ? ai->sequence : 0u);
}

static bool telemetry_local_self_test(void)
{
  uint8_t payload[BREATHSENSE_PAYLOAD_SIZE];

  breathsense_ai_result_t ai = {
    .class_id = BREATHSENSE_AI_COUGH,
    .confidence_percent = 96u,
    .sequence = 0x1234u,
    .timestamp_ms = 0u,
    .valid = true
  };

  build_payload(payload,
                0x4567u,
                -12340,
                56780u,
                true,
                &ai);

  return payload[0] == 0xB5u
         && payload[1] == 1u
         && payload[2] == BREATHSENSE_NODE_ID
         && payload[3] == 0x03u
         && get_u16_le(&payload[4]) == 0x4567u
         && (int16_t)get_u16_le(&payload[6]) == -1234
         && get_u16_le(&payload[8]) == 5678u
         && payload[10] == (uint8_t)BREATHSENSE_AI_COUGH
         && payload[11] == 96u
         && get_u16_le(&payload[12]) == 0x1234u;
}

void breathsense_telemetry_init(void)
{
  breathsense_ai_result_t invalid_ai = {
    .class_id = BREATHSENSE_AI_OTHER,
    .confidence_percent = 0u,
    .sequence = 0u,
    .timestamp_ms = 0u,
    .valid = false
  };

  uint8_t initial_payload[BREATHSENSE_PAYLOAD_SIZE];

  build_payload(initial_payload,
                0u,
                0,
                0u,
                false,
                &invalid_ai);

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    memcpy(latest_payload,
           initial_payload,
           BREATHSENSE_PAYLOAD_SIZE);

    memset(history, 0, sizeof(history));

    latest_payload_ready = true;
    history_write_index = 0u;
    history_count = 0u;
    sensor_sequence = 0u;

    CORE_EXIT_ATOMIC();
  }

  printf("Telemetry initialized: payload=14 history=16\r\n");
  printf("Telemetry self-test: %s\r\n",
         telemetry_local_self_test() ? "PASS" : "FAIL");
}

void breathsense_telemetry_update_sensor(int32_t temperature_mc,
                                         uint32_t humidity_x1000,
                                         bool sensor_valid)
{
  breathsense_ai_result_t ai = {
    .class_id = BREATHSENSE_AI_OTHER,
    .confidence_percent = 0u,
    .sequence = 0u,
    .timestamp_ms = 0u,
    .valid = false
  };

  /*
   * This is called from the sensor task, never from a Bluetooth callback.
   * breathsense_ai_get_latest() performs only an atomic cache copy.
   */
  (void)breathsense_ai_get_latest(&ai);

  uint16_t next_sequence;

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();
    next_sequence = (uint16_t)(sensor_sequence + 1u);
    CORE_EXIT_ATOMIC();
  }

  uint8_t new_payload[BREATHSENSE_PAYLOAD_SIZE];

  build_payload(new_payload,
                next_sequence,
                temperature_mc,
                humidity_x1000,
                sensor_valid,
                &ai);

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    sensor_sequence = next_sequence;

    memcpy(latest_payload,
           new_payload,
           BREATHSENSE_PAYLOAD_SIZE);

    memcpy(history[history_write_index],
           new_payload,
           BREATHSENSE_PAYLOAD_SIZE);

    history_write_index =
      (uint8_t)((history_write_index + 1u)
                % BREATHSENSE_HISTORY_SIZE);

    if (history_count < BREATHSENSE_HISTORY_SIZE) {
      ++history_count;
    }

    latest_payload_ready = true;

    CORE_EXIT_ATOMIC();
  }
}

bool breathsense_telemetry_copy_latest(
  uint8_t payload[BREATHSENSE_PAYLOAD_SIZE])
{
  if (payload == NULL) {
    return false;
  }

  bool ready;

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    ready = latest_payload_ready;

    if (ready) {
      memcpy(payload,
             latest_payload,
             BREATHSENSE_PAYLOAD_SIZE);
    }

    CORE_EXIT_ATOMIC();
  }

  return ready;
}

bool breathsense_telemetry_copy_by_sensor_sequence(
  uint16_t requested_sequence,
  uint8_t payload[BREATHSENSE_PAYLOAD_SIZE])
{
  if (payload == NULL) {
    return false;
  }

  bool found = false;

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    for (uint8_t offset = 0u;
         offset < history_count;
         ++offset) {
      const uint8_t index =
        (uint8_t)((history_write_index
                   + BREATHSENSE_HISTORY_SIZE
                   - 1u
                   - offset)
                  % BREATHSENSE_HISTORY_SIZE);

      if (get_u16_le(&history[index][4])
          == requested_sequence) {
        memcpy(payload,
               history[index],
               BREATHSENSE_PAYLOAD_SIZE);

        found = true;
        break;
      }
    }

    CORE_EXIT_ATOMIC();
  }

  return found;
}

uint16_t breathsense_telemetry_get_latest_sensor_sequence(void)
{
  uint16_t value;

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();
    value = sensor_sequence;
    CORE_EXIT_ATOMIC();
  }

  return value;
}
