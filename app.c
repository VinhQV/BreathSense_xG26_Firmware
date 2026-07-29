/***************************************************************************//**
 * @file app.c
 * @brief Diem khoi tao duy nhat cua ung dung BreathSense.
 *
 * app.c khong chua:
 * - sensor task;
 * - BLE task;
 * - Bluetooth event handler;
 * - AI task;
 * - payload encoder;
 * - polling loop.
 ******************************************************************************/

#include "app.h"

#include "breathsense_ai.h"
#include "breathsense_ble.h"
#include "breathsense_event.h"
#include "breathsense_sensor.h"
#include "breathsense_telemetry.h"

void app_init(void)
{
  /*
   * Queue va telemetry phai san sang truoc cac producer.
   */
  breathsense_event_init();
  breathsense_telemetry_init();

  /*
   * BLE TX flag group phai san sang truoc khi AI va sensor publish du lieu.
   */
  breathsense_ble_init();

  /*
   * Sensor va AI tu tao task/event flag rieng.
   */
  breathsense_sensor_init();
  breathsense_ai_init();
}

void app_process_action(void)
{
  /*
   *
   * Toan bo he thong chay bang:
   * - Micrium OS task;
   * - OSFlagPend/OSFlagPost;
   * - Micrium OS software timer;
   * - Bluetooth stack events;
   * - microphone streaming callback;
   * - event queue.
   */
}
