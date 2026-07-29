/***************************************************************************//**
 * @file breathsense_ble.c
 * @brief Giao dien BLE cap cao cua BreathSense.
 *
 * Module nay che giau hai module noi bo:
 * - breathsense_ble_advertising: connection, GATT, notification;
 * - breathsense_ble_tx: task gui du lieu bang OSFlagPend.
 ******************************************************************************/

#include "breathsense_ble.h"
#include "breathsense_ble_advertising.h"
#include "breathsense_ble_tx.h"

void breathsense_ble_init(void)
{
  /*
   * Khoi tao state GATT truoc, sau do tao BLE TX flag group/task.
   */
  breathsense_ble_advertising_init();
  breathsense_ble_tx_init();
}

void breathsense_ble_notify_cough_ready(void)
{
  breathsense_ble_tx_notify_cough_ready();
}

void breathsense_ble_publish_environment(
  int32_t temperature_mc,
  uint32_t humidity_x1000)
{
  breathsense_ble_tx_publish_environment(
    temperature_mc,
    humidity_x1000);
}
