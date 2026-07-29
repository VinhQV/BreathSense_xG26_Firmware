#ifndef BREATHSENSE_BLE_ADVERTISING_H
#define BREATHSENSE_BLE_ADVERTISING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Callback nhe duoc goi khi trang thai ket noi/subscription thay doi.
 * Callback khong duoc lam cong viec nang.
 */
typedef void (*breathsense_ble_state_changed_callback_t)(void);

void breathsense_ble_advertising_init(void);

/*
 * Dang ky callback de danh thuc BLE TX task.
 */
void breathsense_ble_advertising_set_state_changed_callback(
  breathsense_ble_state_changed_callback_t callback);

/*
 * San sang gui cough notification khi:
 * - Bluetooth stack da boot;
 * - gateway da ket noi;
 * - gateway da subscribe characteristic cough_event.
 */
bool breathsense_ble_advertising_is_ready(void);

/*
 * San sang gui environment notification khi:
 * - Bluetooth stack da boot;
 * - gateway da ket noi;
 * - gateway da subscribe characteristic environment_data.
 */
bool breathsense_ble_advertising_is_environment_ready(void);

bool breathsense_ble_advertising_send_cough_event(
  uint8_t cough_type,
  uint16_t event_counter);

bool breathsense_ble_advertising_send_environment(
  int32_t temperature_mc,
  uint32_t humidity_x1000);

void breathsense_ble_advertising_print_status(void);

#ifdef __cplusplus
}
#endif

#endif /* BREATHSENSE_BLE_ADVERTISING_H */
