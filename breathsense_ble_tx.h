#ifndef BREATHSENSE_BLE_TX_H
#define BREATHSENSE_BLE_TX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Khoi tao task gui BLE theo kieu event-driven.
 *
 * Task se block tai OSFlagPend() khi khong co viec.
 * Khong con vong lap polling va khong con delay 50 ms.
 */
void breathsense_ble_tx_init(void);

/*
 * Goi ham nay NGAY SAU KHI day thanh cong mot cough event
 * vao breathsense_event queue.
 *
 * Event queue giu du lieu.
 * Event flag chi dung de danh thuc BLE TX task.
 */
void breathsense_ble_tx_notify_cough_ready(void);

/*
 * Luu mau nhiet do/do am moi nhat va danh thuc BLE TX task.
 *
 * Neu gateway chua subscribe, mau moi nhat van duoc giu lai.
 * Khi gateway subscribe, task se duoc danh thuc va gui mau dang cho.
 */
void breathsense_ble_tx_publish_environment(
  int32_t temperature_mc,
  uint32_t humidity_x1000);

#ifdef __cplusplus
}
#endif

#endif /* BREATHSENSE_BLE_TX_H */
