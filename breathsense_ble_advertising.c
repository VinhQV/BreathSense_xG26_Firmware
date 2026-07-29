/***************************************************************************//**
 * @file breathsense_ble_advertising.c
 * @brief BLE Peripheral connectable va GATT Notification cho BreathSense.
 *
 * File nay chi phu trach:
 * - tao va khoi dong connectable advertising;
 * - theo doi connection;
 * - theo doi GATT subscription;
 * - encode va gui notification;
 * - bao state changed bang callback nhe.
 *
 * File nay khong chay polling va khong quan ly BLE TX task.
 ******************************************************************************/

#include "breathsense_ble_advertising.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "em_core.h"
#include "gatt_db.h"
#include "sl_bluetooth.h"
#include "sl_status.h"

/*******************************************************************************
 ******************************** CAU HINH *************************************
 ******************************************************************************/

/*
 * Don vi advertising interval la 0.625 ms.
 * 160 x 0.625 ms = 100 ms.
 */
#define BREATHSENSE_ADV_INTERVAL_MIN          160u
#define BREATHSENSE_ADV_INTERVAL_MAX          160u

#define BREATHSENSE_INVALID_HANDLE            0xFFu

#define BREATHSENSE_COUGH_PAYLOAD_SIZE        8u
#define BREATHSENSE_ENV_PAYLOAD_SIZE          4u

#define BREATHSENSE_COUGH_UNKNOWN             0u
#define BREATHSENSE_COUGH_DRY                 1u
#define BREATHSENSE_COUGH_WET                 2u

/*******************************************************************************
 ******************************** TRANG THAI ***********************************
 ******************************************************************************/

static uint8_t advertising_set_handle = BREATHSENSE_INVALID_HANDLE;
static uint8_t connection_handle = BREATHSENSE_INVALID_HANDLE;

static bool bluetooth_booted = false;
static bool connected = false;
static bool cough_notification_enabled = false;
static bool environment_notification_enabled = false;

static uint32_t cough_notification_sent_count = 0u;
static uint32_t environment_notification_sent_count = 0u;
static uint32_t notification_failed_count = 0u;

static sl_status_t last_notification_status = SL_STATUS_NOT_INITIALIZED;

static breathsense_ble_state_changed_callback_t
  state_changed_callback = NULL;

/*******************************************************************************
 ******************************* HAM NOI BO ************************************
 ******************************************************************************/

static void print_status_error(const char *operation,
                               sl_status_t status)
{
  printf("BLE: %s failed: 0x%08lX\r\n",
         operation,
         (unsigned long)status);
}

static void notify_state_changed(void)
{
  /*
   * Callback chi duoc phep post flag hoac lam cong viec rat ngan.
   */
  if (state_changed_callback != NULL) {
    state_changed_callback();
  }
}

static sl_status_t start_connectable_advertising(void)
{
  sl_status_t status;

  if (!bluetooth_booted) {
    return SL_STATUS_NOT_INITIALIZED;
  }

  if (advertising_set_handle == BREATHSENSE_INVALID_HANDLE) {
    status =
      sl_bt_advertiser_create_set(&advertising_set_handle);

    if (status != SL_STATUS_OK) {
      print_status_error("create advertising set", status);
      return status;
    }
  }

  status =
    sl_bt_advertiser_set_timing(
      advertising_set_handle,
      BREATHSENSE_ADV_INTERVAL_MIN,
      BREATHSENSE_ADV_INTERVAL_MAX,
      0u,
      0u);

  if (status != SL_STATUS_OK) {
    print_status_error("set advertising timing", status);
    return status;
  }

  status =
    sl_bt_legacy_advertiser_generate_data(
      advertising_set_handle,
      sl_bt_advertiser_general_discoverable);

  if (status != SL_STATUS_OK) {
    print_status_error("generate advertising data", status);
    return status;
  }

  status =
    sl_bt_legacy_advertiser_start(
      advertising_set_handle,
      sl_bt_legacy_advertiser_connectable);

  if (status != SL_STATUS_OK) {
    print_status_error("start connectable advertising", status);
    return status;
  }

  printf("BLE: connectable advertising started, set=%u, "
         "name=MyDevice_01\r\n",
         advertising_set_handle);

  return SL_STATUS_OK;
}

static void encode_cough_payload(
  uint8_t payload[BREATHSENSE_COUGH_PAYLOAD_SIZE],
  uint8_t cough_type,
  uint16_t event_counter)
{
  memset(payload, 0, BREATHSENSE_COUGH_PAYLOAD_SIZE);

  /*
   * Payload 8 byte:
   * [0]    flags
   * [1]    cough_type
   * [2:5]  event timestamp, hien tai bang 0
   * [6:7]  event_counter little-endian
   */
  payload[0] = 0u;
  payload[1] = cough_type;
  payload[6] = (uint8_t)(event_counter & 0xFFu);
  payload[7] = (uint8_t)((event_counter >> 8u) & 0xFFu);
}

static void encode_environment_payload(
  uint8_t payload[BREATHSENSE_ENV_PAYLOAD_SIZE],
  int32_t temperature_mc,
  uint32_t humidity_x1000,
  int16_t *temperature_x100_out,
  uint16_t *humidity_x100_out)
{
  int32_t temperature_x100_i32 = temperature_mc / 10;
  uint32_t humidity_x100_u32 = humidity_x1000 / 10u;

  int16_t temperature_x100;
  uint16_t humidity_x100;

  if (temperature_x100_i32 > INT16_MAX) {
    temperature_x100_i32 = INT16_MAX;
  } else if (temperature_x100_i32 < INT16_MIN) {
    temperature_x100_i32 = INT16_MIN;
  }

  if (humidity_x100_u32 > UINT16_MAX) {
    humidity_x100_u32 = UINT16_MAX;
  }

  temperature_x100 = (int16_t)temperature_x100_i32;
  humidity_x100 = (uint16_t)humidity_x100_u32;

  /*
   * Payload 4 byte little-endian:
   * [0:1] temperature x100, signed int16
   * [2:3] humidity x100, unsigned int16
   */
  payload[0] = (uint8_t)((uint16_t)temperature_x100 & 0xFFu);
  payload[1] =
    (uint8_t)(((uint16_t)temperature_x100 >> 8u) & 0xFFu);

  payload[2] = (uint8_t)(humidity_x100 & 0xFFu);
  payload[3] =
    (uint8_t)((humidity_x100 >> 8u) & 0xFFu);

  if (temperature_x100_out != NULL) {
    *temperature_x100_out = temperature_x100;
  }

  if (humidity_x100_out != NULL) {
    *humidity_x100_out = humidity_x100;
  }
}

static bool copy_connection_handle(uint8_t *handle)
{
  bool valid;

  if (handle == NULL) {
    return false;
  }

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    *handle = connection_handle;

    valid =
      bluetooth_booted
      && connected
      && (connection_handle != BREATHSENSE_INVALID_HANDLE);

    CORE_EXIT_ATOMIC();
  }

  return valid;
}

static bool write_and_notify(uint16_t characteristic,
                             size_t payload_size,
                             const uint8_t *payload)
{
  sl_status_t status;
  uint8_t local_connection_handle;

  /*
   * Kiem tra payload va sao chep connection handle an toan.
   */
  if ((payload == NULL)
      || !copy_connection_handle(&local_connection_handle)) {
    last_notification_status = SL_STATUS_INVALID_STATE;
    ++notification_failed_count;
    return false;
  }

  /*
   * Gui notification truc tiep.
   *
   * Khong goi sl_bt_gatt_server_write_attribute_value().
   * Characteristic kieu User duoc quan ly boi application
   * va payload duoc truyen truc tiep vao ham notification.
   */
  status =
    sl_bt_gatt_server_send_notification(
      local_connection_handle,
      characteristic,
      payload_size,
      payload);

  last_notification_status = status;

  if (status != SL_STATUS_OK) {
    ++notification_failed_count;

    print_status_error("send GATT notification",
                       status);

    return false;
  }

  return true;
}

/*******************************************************************************
 ***************************** GIAO DIEN CONG KHAI ******************************
 ******************************************************************************/

void breathsense_ble_advertising_init(void)
{
  advertising_set_handle = BREATHSENSE_INVALID_HANDLE;
  connection_handle = BREATHSENSE_INVALID_HANDLE;

  bluetooth_booted = false;
  connected = false;
  cough_notification_enabled = false;
  environment_notification_enabled = false;

  cough_notification_sent_count = 0u;
  environment_notification_sent_count = 0u;
  notification_failed_count = 0u;

  last_notification_status = SL_STATUS_NOT_INITIALIZED;
  state_changed_callback = NULL;

  printf("BLE: GATT transport initialized\r\n");
}

void breathsense_ble_advertising_set_state_changed_callback(
  breathsense_ble_state_changed_callback_t callback)
{
  state_changed_callback = callback;

  /*
   * Goi mot lan ngay sau khi dang ky de task dong bo state hien tai.
   */
  notify_state_changed();
}

bool breathsense_ble_advertising_is_ready(void)
{
  bool ready;

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    ready =
      bluetooth_booted
      && connected
      && cough_notification_enabled
      && (connection_handle != BREATHSENSE_INVALID_HANDLE);

    CORE_EXIT_ATOMIC();
  }

  return ready;
}

bool breathsense_ble_advertising_is_environment_ready(void)
{
  bool ready;

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    ready =
      bluetooth_booted
      && connected
      && environment_notification_enabled
      && (connection_handle != BREATHSENSE_INVALID_HANDLE);

    CORE_EXIT_ATOMIC();
  }

  return ready;
}

bool breathsense_ble_advertising_send_cough_event(
  uint8_t cough_type,
  uint16_t event_counter)
{
  uint8_t payload[BREATHSENSE_COUGH_PAYLOAD_SIZE];

  if ((cough_type != BREATHSENSE_COUGH_UNKNOWN)
      && (cough_type != BREATHSENSE_COUGH_DRY)
      && (cough_type != BREATHSENSE_COUGH_WET)) {
    printf("BLE: invalid cough type=%u\r\n", cough_type);
    return false;
  }

  if (!breathsense_ble_advertising_is_ready()) {
    last_notification_status = SL_STATUS_INVALID_STATE;
    return false;
  }

  encode_cough_payload(payload,
                       cough_type,
                       event_counter);

  if (!write_and_notify(gattdb_cough_event,
                        sizeof(payload),
                        payload)) {
    return false;
  }

  ++cough_notification_sent_count;

  printf("BLE TX: "
         "%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
         payload[0],
         payload[1],
         payload[2],
         payload[3],
         payload[4],
         payload[5],
         payload[6],
         payload[7]);

  return true;
}

bool breathsense_ble_advertising_send_environment(
  int32_t temperature_mc,
  uint32_t humidity_x1000)
{
  uint8_t payload[BREATHSENSE_ENV_PAYLOAD_SIZE];

  int16_t temperature_x100;
  uint16_t humidity_x100;
  int32_t temperature_abs;

  if (!breathsense_ble_advertising_is_environment_ready()) {
    last_notification_status = SL_STATUS_INVALID_STATE;
    return false;
  }

  encode_environment_payload(payload,
                             temperature_mc,
                             humidity_x1000,
                             &temperature_x100,
                             &humidity_x100);

  if (!write_and_notify(gattdb_environment_data,
                        sizeof(payload),
                        payload)) {
    return false;
  }

  ++environment_notification_sent_count;

  temperature_abs =
    (temperature_x100 < 0)
    ? -(int32_t)temperature_x100
    : (int32_t)temperature_x100;

  printf("BLE ENV TX: %02X %02X %02X %02X "
         "T=%s%ld.%02ld C RH=%lu.%02lu %%\r\n",
         payload[0],
         payload[1],
         payload[2],
         payload[3],
         (temperature_x100 < 0) ? "-" : "",
         (long)(temperature_abs / 100),
         (long)(temperature_abs % 100),
         (unsigned long)(humidity_x100 / 100u),
         (unsigned long)(humidity_x100 % 100u));

  return true;
}

void breathsense_ble_advertising_print_status(void)
{
  uint8_t local_adv_set;
  uint8_t local_connection;
  bool local_booted;
  bool local_connected;
  bool local_cough_enabled;
  bool local_environment_enabled;

  uint32_t local_cough_sent;
  uint32_t local_environment_sent;
  uint32_t local_failed;
  sl_status_t local_last_status;

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    local_adv_set = advertising_set_handle;
    local_connection = connection_handle;

    local_booted = bluetooth_booted;
    local_connected = connected;
    local_cough_enabled = cough_notification_enabled;
    local_environment_enabled = environment_notification_enabled;

    local_cough_sent = cough_notification_sent_count;
    local_environment_sent = environment_notification_sent_count;
    local_failed = notification_failed_count;
    local_last_status = last_notification_status;

    CORE_EXIT_ATOMIC();
  }

  printf("BLE STATUS: boot=%u adv_set=%u conn=%u connected=%u "
         "cough_sub=%u env_sub=%u cough_sent=%lu env_sent=%lu "
         "failed=%lu last=0x%08lX\r\n",
         local_booted ? 1u : 0u,
         local_adv_set,
         local_connection,
         local_connected ? 1u : 0u,
         local_cough_enabled ? 1u : 0u,
         local_environment_enabled ? 1u : 0u,
         (unsigned long)local_cough_sent,
         (unsigned long)local_environment_sent,
         (unsigned long)local_failed,
         (unsigned long)local_last_status);
}

/*******************************************************************************
 *************************** BLUETOOTH EVENT HANDLER ****************************
 ******************************************************************************/

void sl_bt_on_event(sl_bt_msg_t *evt)
{
  sl_status_t status;

  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id:
      advertising_set_handle = BREATHSENSE_INVALID_HANDLE;
      connection_handle = BREATHSENSE_INVALID_HANDLE;

      bluetooth_booted = true;
      connected = false;
      cough_notification_enabled = false;
      environment_notification_enabled = false;

      printf("BLE: stack booted\r\n");

      status = start_connectable_advertising();

      if (status != SL_STATUS_OK) {
        print_status_error("boot advertising", status);
      }

      notify_state_changed();
      break;

    case sl_bt_evt_connection_opened_id:
      connection_handle =
        evt->data.evt_connection_opened.connection;

      connected = true;
      cough_notification_enabled = false;
      environment_notification_enabled = false;

      printf("BLE: gateway connected, handle=%u\r\n",
             connection_handle);

      notify_state_changed();
      break;

    case sl_bt_evt_gatt_server_characteristic_status_id:
    {
      const uint8_t event_connection =
        evt->data.evt_gatt_server_characteristic_status.connection;

      const uint16_t characteristic =
        evt->data.evt_gatt_server_characteristic_status.characteristic;

      const uint8_t status_flags =
        evt->data.evt_gatt_server_characteristic_status.status_flags;

      const uint16_t client_config_flags =
        evt->data.evt_gatt_server_characteristic_status
          .client_config_flags;

      bool relevant_state_changed = false;

      if ((event_connection == connection_handle)
          && ((status_flags
               & sl_bt_gatt_server_client_config) != 0u)) {
        const bool enabled =
          ((client_config_flags
            & sl_bt_gatt_server_notification) != 0u);

        if (characteristic == gattdb_cough_event) {
          cough_notification_enabled = enabled;
          relevant_state_changed = true;

          printf("BLE: cough notification %s\r\n",
                 enabled ? "enabled" : "disabled");
        } else if (characteristic == gattdb_environment_data) {
          environment_notification_enabled = enabled;
          relevant_state_changed = true;

          printf("BLE: environment notification %s\r\n",
                 enabled ? "enabled" : "disabled");
        }
      }

      if (relevant_state_changed) {
        notify_state_changed();
      }

      break;
    }

    case sl_bt_evt_connection_closed_id:
      printf("BLE: gateway disconnected, reason=0x%04X\r\n",
             evt->data.evt_connection_closed.reason);

      connection_handle = BREATHSENSE_INVALID_HANDLE;
      connected = false;
      cough_notification_enabled = false;
      environment_notification_enabled = false;

      /*
       * Danh thuc task de task dung gui ngay khi connection da dong.
       */
      notify_state_changed();

      status = start_connectable_advertising();

      if (status != SL_STATUS_OK) {
        print_status_error("restart advertising", status);
      }

      break;

    default:
      break;
  }
}
