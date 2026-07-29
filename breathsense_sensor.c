/***************************************************************************//**
 * @file breathsense_sensor.c
 * @brief Si7021 event-driven bang OSFlagPend va Silicon Labs sleeptimer.
 *
 * Luong xu ly:
 *
 *   periodic timer het han
 *            |
 *            v
 *   OSFlagPost(SAMPLE_READY)
 *            |
 *            v
 *   sensor task dang OSFlagPend duoc danh thuc
 *            |
 *            v
 *   doc Si7021 -> telemetry -> BLE publish
 *
 * Khong co:
 * - vong lap kiem tra trang thai lien tuc;
 * - delay dinh ky trong sensor task;
 * - BLE send truc tiep tu timer callback.
 ******************************************************************************/

#include "breathsense_sensor.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "breathsense_ble.h"
#include "breathsense_telemetry.h"
#include "em_assert.h"
#include "em_core.h"
#include "os.h"
#include "sl_board_control.h"
#include "sl_i2cspm_instances.h"
#include "sl_si70xx.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"

/*******************************************************************************
 ******************************** CAU HINH ************************************
 ******************************************************************************/

#define SENSOR_TASK_STACK_SIZE             512u
#define SENSOR_TASK_PRIORITY               25u

#define SENSOR_STARTUP_DELAY_MS            50u
#define SENSOR_SAMPLE_PERIOD_MS            5000u

#define SENSOR_FLAG_STARTUP_READY          ((OS_FLAGS)1u << 0u)
#define SENSOR_FLAG_SAMPLE_READY           ((OS_FLAGS)1u << 1u)

/*******************************************************************************
 ******************************** BO NHO TINH *********************************
 ******************************************************************************/

static OS_TCB sensor_task_tcb;
static CPU_STK sensor_task_stack[SENSOR_TASK_STACK_SIZE];
static CPU_CHAR sensor_task_name[] = "BreathSense Si7021";

static OS_FLAG_GRP sensor_flag_group;
static CPU_CHAR sensor_flag_group_name[] = "BreathSense Sensor Flags";

static sl_sleeptimer_timer_handle_t sensor_startup_timer;
static sl_sleeptimer_timer_handle_t sensor_sample_timer;

static bool sensor_initialized = false;
static bool sensor_flag_group_ready = false;

static breathsense_sensor_result_t latest_result;

/*******************************************************************************
 ***************************** HAM NOI BO - FLAG *******************************
 ******************************************************************************/

static void sensor_post_flag(OS_FLAGS flags)
{
  RTOS_ERR err;

  if (!sensor_flag_group_ready) {
    return;
  }

  (void)OSFlagPost(&sensor_flag_group,
                   flags,
                   OS_OPT_POST_FLAG_SET,
                   &err);

  /*
   * Timer callback khong printf.
   * Neu can debug, co the them counter loi atomic tai day.
   */
}

static void sensor_startup_timer_callback(
  sl_sleeptimer_timer_handle_t *handle,
  void *data)
{
  (void)handle;
  (void)data;

  sensor_post_flag(SENSOR_FLAG_STARTUP_READY);
}

static void sensor_sample_timer_callback(
  sl_sleeptimer_timer_handle_t *handle,
  void *data)
{
  (void)handle;
  (void)data;

  sensor_post_flag(SENSOR_FLAG_SAMPLE_READY);
}

/*******************************************************************************
 ***************************** HAM NOI BO - DATA *******************************
 ******************************************************************************/

static void sensor_publish_result(int32_t temperature_mc,
                                  uint32_t humidity_x1000,
                                  bool valid)
{
  breathsense_sensor_result_t result;

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    result.temperature_mc = temperature_mc;
    result.humidity_x1000 = humidity_x1000;
    result.sequence = latest_result.sequence + 1u;
    result.valid = valid;

    latest_result = result;

    CORE_EXIT_ATOMIC();
  }

  breathsense_telemetry_update_sensor(
    temperature_mc,
    humidity_x1000,
    valid);

  if (valid) {
    breathsense_ble_publish_environment(
      temperature_mc,
      humidity_x1000);
  }
}

static void sensor_print_values(int32_t temperature_mc,
                                uint32_t humidity_x1000)
{
  uint32_t temperature_abs;

  if (temperature_mc < 0) {
    temperature_abs = (uint32_t)(-temperature_mc);

    printf("Si7021: T=-%lu.%03lu C, RH=%lu.%03lu %%\r\n",
           (unsigned long)(temperature_abs / 1000u),
           (unsigned long)(temperature_abs % 1000u),
           (unsigned long)(humidity_x1000 / 1000u),
           (unsigned long)(humidity_x1000 % 1000u));
  } else {
    temperature_abs = (uint32_t)temperature_mc;

    printf("Si7021: T=%lu.%03lu C, RH=%lu.%03lu %%\r\n",
           (unsigned long)(temperature_abs / 1000u),
           (unsigned long)(temperature_abs % 1000u),
           (unsigned long)(humidity_x1000 / 1000u),
           (unsigned long)(humidity_x1000 % 1000u));
  }
}

static void sensor_measure_once(void)
{
  int32_t temperature_mc = 0;
  uint32_t humidity_x1000 = 0u;

  const sl_status_t status =
    sl_si70xx_measure_rh_and_temp(
      sl_i2cspm_sensor,
      SI7021_ADDR,
      &humidity_x1000,
      &temperature_mc);

  if (status == SL_STATUS_OK) {
    sensor_publish_result(
      temperature_mc,
      humidity_x1000,
      true);

    sensor_print_values(
      temperature_mc,
      humidity_x1000);
  } else {
    sensor_publish_result(0, 0u, false);

    printf("Si7021 measurement failed: 0x%08lX\r\n",
           (unsigned long)status);
  }
}

/*******************************************************************************
 ******************************* SENSOR TASK **********************************
 ******************************************************************************/

static void sensor_task(void *arg)
{
  RTOS_ERR err;
  sl_status_t status;
  uint8_t device_id = 0u;

  (void)arg;

  printf("Si7021 task started: event-driven\r\n");

  status = sl_board_enable_sensor(SL_BOARD_SENSOR_RHT);

  if (status != SL_STATUS_OK) {
    printf("Si7021 power enable failed: 0x%08lX\r\n",
           (unsigned long)status);
    return;
  }

  /*
   * Khoi dong one-shot timer thay cho delay block 50 ms.
   */
  status =
    sl_sleeptimer_start_timer_ms(
      &sensor_startup_timer,
      SENSOR_STARTUP_DELAY_MS,
      sensor_startup_timer_callback,
      DEF_NULL,
      0u,
      0u);

  if (status != SL_STATUS_OK) {
    printf("Si7021 startup timer failed: 0x%08lX\r\n",
           (unsigned long)status);
    return;
  }

  /*
   * Task ngu den khi startup timer post flag.
   */
  (void)OSFlagPend(&sensor_flag_group,
                   SENSOR_FLAG_STARTUP_READY,
                   0u,
                   OS_OPT_PEND_FLAG_SET_ALL
                   | OS_OPT_PEND_FLAG_CONSUME
                   | OS_OPT_PEND_BLOCKING,
                   DEF_NULL,
                   &err);

  if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
    printf("Si7021 startup pend failed: %ld\r\n",
           (long)RTOS_ERR_CODE_GET(err));
    return;
  }

  status =
    sl_si70xx_init(sl_i2cspm_sensor,
                   SI7021_ADDR);

  if (status != SL_STATUS_OK) {
    printf("Si7021 init failed: 0x%08lX\r\n",
           (unsigned long)status);
    return;
  }

  if (!sl_si70xx_present(sl_i2cspm_sensor,
                         SI7021_ADDR,
                         &device_id)) {
    printf("Si7021 not found on I2C bus\r\n");
    return;
  }

  printf("Si7021 detected, device ID=0x%02X\r\n",
         device_id);

  /*
   * Do mot lan ngay sau khi khoi tao thanh cong.
   */
  sensor_measure_once();

  /*
   * Bat periodic timer.
   * Tu day sensor task chi duoc danh thuc khi timer post SAMPLE_READY
   * hoac khi module khac goi breathsense_sensor_trigger_now().
   */
  status =
    sl_sleeptimer_start_periodic_timer_ms(
      &sensor_sample_timer,
      SENSOR_SAMPLE_PERIOD_MS,
      sensor_sample_timer_callback,
      DEF_NULL,
      0u,
      0u);

  if (status != SL_STATUS_OK) {
    printf("Si7021 periodic timer failed: 0x%08lX\r\n",
           (unsigned long)status);
    return;
  }

  while (DEF_TRUE) {
    const OS_FLAGS flags =
      OSFlagPend(&sensor_flag_group,
                 SENSOR_FLAG_SAMPLE_READY,
                 0u,
                 OS_OPT_PEND_FLAG_SET_ANY
                 | OS_OPT_PEND_FLAG_CONSUME
                 | OS_OPT_PEND_BLOCKING,
                 DEF_NULL,
                 &err);

    if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
      /*
       * Khong return vi task runtime khong nen chet do loi pend tam thoi.
       */
      continue;
    }

    if ((flags & SENSOR_FLAG_SAMPLE_READY) != 0u) {
      sensor_measure_once();
    }
  }
}

/*******************************************************************************
 ***************************** GIAO DIEN CONG KHAI ******************************
 ******************************************************************************/

void breathsense_sensor_init(void)
{
  RTOS_ERR err;

  if (sensor_initialized) {
    return;
  }

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    latest_result.temperature_mc = 0;
    latest_result.humidity_x1000 = 0u;
    latest_result.sequence = 0u;
    latest_result.valid = false;

    CORE_EXIT_ATOMIC();
  }

  OSFlagCreate(&sensor_flag_group,
               sensor_flag_group_name,
               0u,
               &err);

  EFM_ASSERT(RTOS_ERR_CODE_GET(err) == RTOS_ERR_NONE);

  sensor_flag_group_ready = true;

  OSTaskCreate(&sensor_task_tcb,
               sensor_task_name,
               sensor_task,
               DEF_NULL,
               SENSOR_TASK_PRIORITY,
               &sensor_task_stack[0],
               SENSOR_TASK_STACK_SIZE / 10u,
               SENSOR_TASK_STACK_SIZE,
               0u,
               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CLR,
               &err);

  EFM_ASSERT(RTOS_ERR_CODE_GET(err) == RTOS_ERR_NONE);

  sensor_initialized = true;
}

void breathsense_sensor_trigger_now(void)
{
  sensor_post_flag(SENSOR_FLAG_SAMPLE_READY);
}

bool breathsense_sensor_get_latest(
  breathsense_sensor_result_t *result)
{
  if (result == NULL) {
    return false;
  }

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();
    *result = latest_result;
    CORE_EXIT_ATOMIC();
  }

  return result->valid;
}
