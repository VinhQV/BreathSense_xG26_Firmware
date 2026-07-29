#ifndef BREATHSENSE_BLE_H
#define BREATHSENSE_BLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Khoi tao BLE Peripheral/GATT va BLE TX task.
 */
void breathsense_ble_init(void);

/*
 * AI goi ham nay sau khi push cough event vao event queue thanh cong.
 */
void breathsense_ble_notify_cough_ready(void);

/*
 * Sensor goi ham nay khi co mau nhiet do/do am moi.
 */
void breathsense_ble_publish_environment(
  int32_t temperature_mc,
  uint32_t humidity_x1000);

#ifdef __cplusplus
}
#endif

#endif /* BREATHSENSE_BLE_H */
