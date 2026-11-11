#include "breathsense_pendant.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "em_core.h"
#include "em_iadc.h"
#include "os.h"
#include "sl_board_control.h"
#include "sl_clock_manager.h"
#include "sl_icm40627.h"
#include "sl_sleeptimer.h"

#define SAMPLE_PERIOD_MS 40u
#define WINDOW_SAMPLES 50u
#define SAMPLE_READY ((OS_FLAGS)1u)
#define PENDANT_TASK_PRIORITY 20u
#define PENDANT_STACK_SIZE 512u
#define MOTION_DELTA_G 0.030f
#define MOTION_SAMPLES_MIN 6u
#define FLAT_SAMPLES_MIN 48u
#define ADC_SAMPLES 8u
#define ADC_FULL_SCALE 4095u

typedef enum {
  WEAR_UNKNOWN,
  WEAR_LIKELY_WORN,
  WEAR_LIKELY_OFF_BODY
} wear_state_t;

static OS_TCB task;
static CPU_STK stack[PENDANT_STACK_SIZE];
static OS_FLAG_GRP flags;
static sl_sleeptimer_timer_handle_t timer;
static uint8_t status[BREATHSENSE_PENDANT_STATUS_SIZE] = {1u, 0u, 0u, 0u, UINT8_MAX, UINT8_MAX};

static void sample_due(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  (void)data;
  RTOS_ERR err;
  (void)OSFlagPost(&flags, SAMPLE_READY, OS_OPT_POST_FLAG_SET, &err);
}

static bool start_imu(void)
{
  if (sl_board_enable_sensor(SL_BOARD_SENSOR_IMU) != SL_STATUS_OK) {
    return false;
  }

  sl_sleeptimer_delay_millisecond(10u);
  return sl_icm40627_init() == SL_STATUS_OK
         && sl_icm40627_accel_set_full_scale(sl_accelFS_2g) == SL_STATUS_OK
         && sl_icm40627_accel_set_sample_rate(sl_accelODR_50Hz) == SL_STATUS_OK
         && sl_icm40627_accel_enter_low_power_mode(true) == SL_STATUS_OK;
}

static uint16_t read_supply_mv(void)
{
  if (sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_IADC0) != SL_STATUS_OK) {
    return 0u;
  }

  IADC_Init_t init = IADC_INIT_DEFAULT;
  IADC_AllConfigs_t configs = IADC_ALLCONFIGS_DEFAULT;
  IADC_InitSingle_t single = IADC_INITSINGLE_DEFAULT;
  IADC_SingleInput_t input = IADC_SINGLEINPUT_DEFAULT;

  init.srcClkPrescale = IADC_calcSrcClkPrescale(IADC0, 2000000u, 0u);
  configs.configs[0].adcClkPrescale =
    IADC_calcAdcClkPrescale(IADC0, 1000000u, 0u, iadcCfgModeNormal,
                            init.srcClkPrescale);
  configs.configs[0].osrHighSpeed = iadcCfgOsrHighSpeed2x;
  input.posInput = iadcPosInputAvdd;
  IADC_init(IADC0, &init, &configs);
  IADC_initSingle(IADC0, &single, &input);

  uint32_t sum = 0u;
  bool complete = true;
  const uint32_t timeout = sl_sleeptimer_ms_to_tick(2u);
  for (unsigned i = 0u; i < ADC_SAMPLES; ++i) {
    IADC_command(IADC0, iadcCmdStartSingle);
    const uint32_t start = sl_sleeptimer_get_tick_count();
    while (IADC_getSingleFifoCnt(IADC0) == 0u
           && sl_sleeptimer_get_tick_count() - start < timeout) {
    }
    if (IADC_getSingleFifoCnt(IADC0) == 0u) {
      complete = false;
      break;
    }
    sum += IADC_pullSingleFifoResult(IADC0).data;
  }
  IADC_reset(IADC0);
  (void)sl_clock_manager_disable_bus_clock(SL_BUS_CLOCK_IADC0);

  /* The internal IADC path measures AVDD/4 against the internal reference. */
  const uint32_t scale = ADC_FULL_SCALE * ADC_SAMPLES;
  return complete ? (uint16_t)((4u * sum * configs.configs[0].vRef
                                + scale / 2u) / scale) : 0u;
}

static void publish_status(wear_state_t wear, bool imu_valid,
                           uint16_t supply_mv, uint8_t motion, uint8_t flat)
{
  CORE_DECLARE_IRQ_STATE;
  CORE_ENTER_ATOMIC();
  status[1] = (uint8_t)wear;
  status[2] = (uint8_t)supply_mv;
  status[3] = (uint8_t)(supply_mv >> 8);
  status[4] = imu_valid ? motion : UINT8_MAX;
  status[5] = imu_valid ? flat : UINT8_MAX;
  CORE_EXIT_ATOMIC();
}

void breathsense_pendant_status(uint8_t data[BREATHSENSE_PENDANT_STATUS_SIZE])
{
  CORE_DECLARE_IRQ_STATE;
  CORE_ENTER_ATOMIC();
  for (unsigned i = 0u; i < BREATHSENSE_PENDANT_STATUS_SIZE; ++i) {
    data[i] = status[i];
  }
  CORE_EXIT_ATOMIC();
}

static void pendant_task(void *arg)
{
  (void)arg;
  const bool imu_ready = start_imu();
  printf("PENDANT: ICM-40627 %s\r\n", imu_ready ? "ready" : "unavailable");

  if (sl_sleeptimer_start_periodic_timer_ms(&timer, SAMPLE_PERIOD_MS,
                                           sample_due, NULL, 0u, 0u) != SL_STATUS_OK) {
    printf("PENDANT: timer failed\r\n");
    return;
  }

  wear_state_t wear = WEAR_UNKNOWN;
  wear_state_t candidate = WEAR_UNKNOWN;
  uint8_t candidate_windows = 0u;
  uint8_t samples = 0u;
  uint8_t valid = 0u;
  uint8_t motion = 0u;
  uint8_t flat = 0u;
  float previous[3] = {0.0f};
  bool previous_valid = false;
  uint16_t supply_mv = read_supply_mv();
  uint8_t voltage_windows = 0u;

  for (;;) {
    RTOS_ERR err;
    (void)OSFlagPend(&flags, SAMPLE_READY, 0u,
                     OS_OPT_PEND_FLAG_SET_ANY | OS_OPT_PEND_FLAG_CONSUME
                     | OS_OPT_PEND_BLOCKING, NULL, &err);
    if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
      continue;
    }

    float axes[3];
    const bool sample_valid = imu_ready
      && sl_icm40627_accel_read_data(axes) == SL_STATUS_OK
      && fabsf(axes[0]) + fabsf(axes[1]) + fabsf(axes[2]) > 0.25f;
    if (sample_valid) {
      ++valid;
      if (previous_valid
          && (fabsf(axes[0] - previous[0]) + fabsf(axes[1] - previous[1])
              + fabsf(axes[2] - previous[2])) >= MOTION_DELTA_G) {
        ++motion;
      }
      if (fabsf(axes[2]) >= 0.90f && fabsf(axes[0]) < 0.40f
          && fabsf(axes[1]) < 0.40f) {
        ++flat;
      }
      for (unsigned axis = 0u; axis < 3u; ++axis) {
        previous[axis] = axes[axis];
      }
    }
    previous_valid = sample_valid;

    if (++samples < WINDOW_SAMPLES) {
      continue;
    }

    const bool imu_valid = valid >= FLAT_SAMPLES_MIN;
    wear_state_t proposed = WEAR_UNKNOWN;
    if (imu_valid && motion >= MOTION_SAMPLES_MIN && flat < WINDOW_SAMPLES / 2u) {
      proposed = WEAR_LIKELY_WORN;
    } else if (imu_valid && motion <= 1u && flat >= FLAT_SAMPLES_MIN) {
      proposed = WEAR_LIKELY_OFF_BODY;
    }

    if (!imu_valid) {
      wear = WEAR_UNKNOWN;
      candidate = WEAR_UNKNOWN;
      candidate_windows = 0u;
    } else {
      if (proposed != candidate) {
        candidate = proposed;
        candidate_windows = 0u;
      }
      const uint8_t required = proposed == WEAR_LIKELY_OFF_BODY ? 4u : 2u;
      if (candidate_windows < required && ++candidate_windows == required) {
        wear = candidate;
      }
    }

    if (++voltage_windows == 5u) {
      supply_mv = read_supply_mv();
      voltage_windows = 0u;
    }
    publish_status(wear, imu_valid, supply_mv, motion, flat);
    printf("PENDANT: wear=%u vmcu_mv=%u imu=%u motion=%u flat=%u\r\n",
           (unsigned)wear, supply_mv, (unsigned)imu_valid, motion, flat);
    samples = 0u;
    valid = 0u;
    motion = 0u;
    flat = 0u;
  }
}

void breathsense_pendant_init(void)
{
  RTOS_ERR err;
  static CPU_CHAR task_name[] = "BreathSense Pendant";
  static CPU_CHAR flag_name[] = "Pendant sample";

  OSFlagCreate(&flags, flag_name, 0u, &err);
  if (RTOS_ERR_CODE_GET(err) == RTOS_ERR_NONE) {
    OSTaskCreate(&task, task_name, pendant_task, NULL, PENDANT_TASK_PRIORITY,
                 stack, PENDANT_STACK_SIZE / 10u, PENDANT_STACK_SIZE,
                 0u, 0u, NULL, OS_OPT_TASK_STK_CLR, &err);
  }
  if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
    printf("PENDANT: task initialization failed\r\n");
  }
}
