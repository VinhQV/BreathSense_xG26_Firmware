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
#include "sl_gpio.h"
#include "sl_clock_manager.h"
#include "em_i2c.h"

/*******************************************************************************
 ******************************** CAU HINH ************************************
 ******************************************************************************/

#define SENSOR_TASK_STACK_SIZE             512u
#define SENSOR_TASK_PRIORITY               25u

#define SENSOR_STARTUP_DELAY_MS            50u
#define SENSOR_SAMPLE_PERIOD_MS            5000u

#define SENSOR_FLAG_STARTUP_READY          ((OS_FLAGS)1u << 0u)
#define SENSOR_FLAG_CONTROL ((OS_FLAGS)1u << 2u)
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

static volatile bool sensor_paused = true;
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

static void sensor_bus_power(bool on)
{
  const sl_gpio_t scl = { SL_GPIO_PORT_C, 4u }, sda = { SL_GPIO_PORT_C, 5u };
  (void)sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_I2C1);
  I2C_Enable(I2C1, on);
  (void)sl_gpio_set_pin_mode(&scl, on ? SL_GPIO_MODE_WIRED_AND_PULLUP : SL_GPIO_MODE_DISABLED, on);
  (void)sl_gpio_set_pin_mode(&sda, on ? SL_GPIO_MODE_WIRED_AND_PULLUP : SL_GPIO_MODE_DISABLED, on);
  if (!on) { (void)sl_clock_manager_disable_bus_clock(SL_BUS_CLOCK_I2C1); }
}

static void sensor_task(void *arg)
{
  (void)arg;
  bool powered = false, ready = false;
  sensor_bus_power(false);
  printf("Si7021 task: standby until confirmed wear\r\n");
  for (;;) {
    RTOS_ERR err;
    const OS_FLAGS pending = OSFlagPend(&sensor_flag_group,
      SENSOR_FLAG_CONTROL | SENSOR_FLAG_SAMPLE_READY | SENSOR_FLAG_STARTUP_READY,
      0u, OS_OPT_PEND_FLAG_SET_ANY | OS_OPT_PEND_FLAG_CONSUME | OS_OPT_PEND_BLOCKING,
      NULL, &err);
    if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) { continue; }
    if (sensor_paused) {
      (void)sl_sleeptimer_stop_timer(&sensor_startup_timer);
      (void)sl_sleeptimer_stop_timer(&sensor_sample_timer);
      if (powered) {
        sensor_bus_power(false);
        (void)sl_board_disable_sensor(SL_BOARD_SENSOR_RHT);
        sensor_publish_result(0, 0u, false);
        printf("POWER: Si7021 off\r\n");
      }
      powered = ready = false;
      continue;
    }
    if (!powered) {
      if (sl_board_enable_sensor(SL_BOARD_SENSOR_RHT) != SL_STATUS_OK) { continue; }
      sensor_bus_power(true);
      powered = true;
      if (sl_sleeptimer_start_timer_ms(&sensor_startup_timer, SENSOR_STARTUP_DELAY_MS,
          sensor_startup_timer_callback, NULL, 0u, 0u) != SL_STATUS_OK) {
        (void)sl_board_disable_sensor(SL_BOARD_SENSOR_RHT);
        sensor_bus_power(false);
        powered = false;
      }
      continue;
    }
    if (pending & SENSOR_FLAG_STARTUP_READY) {
      uint8_t id = 0u;
      ready = sl_si70xx_init(sl_i2cspm_sensor, SI7021_ADDR) == SL_STATUS_OK
        && sl_si70xx_present(sl_i2cspm_sensor, SI7021_ADDR, &id);
      printf("Si7021: resume %s id=0x%02x\r\n", ready ? "ready" : "failed", id);
      if (ready) {
        if (!sensor_paused) { sensor_measure_once(); }
        (void)sl_sleeptimer_start_periodic_timer_ms(&sensor_sample_timer,
          SENSOR_SAMPLE_PERIOD_MS, sensor_sample_timer_callback, NULL, 0u, 0u);
      } else {
        /* Retry only while active; a control signal cancels this timer on sleep. */
        (void)sl_sleeptimer_start_timer_ms(&sensor_startup_timer, SENSOR_SAMPLE_PERIOD_MS,
          sensor_startup_timer_callback, NULL, 0u, 0u);
      }
    } else if (ready && (pending & SENSOR_FLAG_SAMPLE_READY) && !sensor_paused) {
      sensor_measure_once();
    }
  }
}

void breathsense_sensor_set_paused(bool paused)
{
  CORE_DECLARE_IRQ_STATE;
  CORE_ENTER_ATOMIC();
  bool changed = sensor_paused != paused;
  sensor_paused = paused;
  CORE_EXIT_ATOMIC();
  if (changed) { sensor_post_flag(SENSOR_FLAG_CONTROL); }
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
