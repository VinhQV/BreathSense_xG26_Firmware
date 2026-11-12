#include "breathsense_pendant.h"

#include "breathsense_ai.h"
#include "breathsense_sensor.h"
#include "breathsense_ble_advertising.h"
#include "breathsense_wear_policy.h"
#include "sl_icm40627_config.h"
#include "sl_gpio.h"
#include "sl_hal_gpio.h"
#include "sl_power_manager.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include "em_core.h"
#include "em_iadc.h"
#include "em_eusart.h"
#include "os.h"
#include "sl_board_control.h"
#include "sl_clock_manager.h"
#include "sl_icm40627.h"
#include "sl_sleeptimer.h"

#define SAMPLE_PERIOD_MS 40u
#define WINDOW_SAMPLES 50u
#define SAMPLE_READY ((OS_FLAGS)1u)
#define MOTION_WAKE ((OS_FLAGS)2u)
#define PENDANT_TASK_PRIORITY 20u
#define PENDANT_STACK_SIZE 512u
#define MOTION_DELTA_G 0.030f
#define FLAT_SAMPLES_MIN 48u
#define ADC_SAMPLES 8u
#define ADC_FULL_SCALE 4095u

static int32_t motion_interrupt = SL_GPIO_INTERRUPT_UNAVAILABLE;
static const sl_gpio_t motion_pin = { SL_ICM40627_INT_PORT, SL_ICM40627_INT_PIN };
/* Readable diagnostics; ISR only increments, never logs or accesses SPI. */
volatile uint32_t breathsense_em2_entries;
volatile uint32_t breathsense_motion_wakes;
volatile uint8_t breathsense_power_mode; /* 0=verify, 1=active, 2=WOM sleep, 3=fault */
static sl_power_manager_em_transition_event_handle_t energy_handle;
static void energy_transition(sl_power_manager_em_t from, sl_power_manager_em_t to)
{
  (void)from;
  if (to == SL_POWER_MANAGER_EM2) { ++breathsense_em2_entries; }
}
static const sl_power_manager_em_transition_event_info_t energy_info = {
  SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM2, energy_transition
};

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

static void motion_irq(uint8_t number, void *context)
{
  (void)context;
  (void)sl_gpio_disable_interrupts(1u << number);
  ++breathsense_motion_wakes;
  RTOS_ERR err;
  (void)OSFlagPost(&flags, MOTION_WAKE, OS_OPT_POST_FLAG_SET, &err);
}

static bool configure_motion_irq(void)
{
  if (sl_gpio_set_pin_mode(&motion_pin, SL_GPIO_MODE_INPUT_PULL, 1u) != SL_STATUS_OK) { return false; }
  if (sl_gpio_configure_external_interrupt(&motion_pin, &motion_interrupt,
      SL_GPIO_INTERRUPT_FALLING_EDGE, motion_irq, NULL) != SL_STATUS_OK) { return false; }
  (void)sl_gpio_disable_interrupts(1u << motion_interrupt);
  return true;
}

static bool arm_motion_sleep(void)
{
  uint8_t config;
  (void)sl_icm40627_enable_interrupt(false, false);
  (void)sl_icm40627_write_register(ICM40627_REG_SMD_CONFIG, 0u);
  (void)sl_icm40627_accel_set_sample_rate(sl_accelODR_12_5Hz);
  /* Accel LP, gyro OFF, temperature OFF, RC idle OFF. */
  (void)sl_icm40627_write_register(ICM40627_REG_PWR_MGMT0, 0x22u);
  /* Fix SDK init's OR-with-zero: INT_ASYNC_RESET really must be cleared. */
  (void)sl_icm40627_read_register(ICM40627_REG_INT_CONFIG1, 1u, &config);
  (void)sl_icm40627_write_register(ICM40627_REG_INT_CONFIG1, config & ~(1u << 4));
  /* 24 * 3.9 mg = ~94 mg; OR axes, compare consecutive samples, WOM enabled. */
  (void)sl_icm40627_write_register(ICM40627_REG_ACCEL_WOM_X_THR, 24u);
  (void)sl_icm40627_write_register(ICM40627_REG_ACCEL_WOM_Y_THR, 24u);
  (void)sl_icm40627_write_register(ICM40627_REG_ACCEL_WOM_Z_THR, 24u);
  (void)sl_icm40627_write_register(ICM40627_REG_SMD_CONFIG, 0x05u);
  (void)sl_icm40627_read_register(ICM40627_REG_SMD_CONFIG, 1u, &config);
  if (config != 0x05u) { return false; }
  (void)sl_icm40627_read_register(ICM40627_REG_PWR_MGMT0, 1u, &config);
  if (config != 0x22u) { return false; }
  (void)sl_icm40627_read_register(ICM40627_REG_INT_STATUS2, 1u, &config);
  sl_hal_gpio_clear_interrupts(1u << motion_interrupt);
  (void)sl_gpio_enable_interrupts(1u << motion_interrupt);
  (void)sl_icm40627_enable_interrupt(false, true);
  /* The SDK SPI driver cannot cross EM2 with an active EUSART transfer state.
   * Hold EM1 while sampling; explicitly shut SPI down before releasing it. */
  EUSART_Enable(SL_ICM40627_SPI_EUSART_PERIPHERAL, eusartDisable);
  (void)sl_clock_manager_disable_bus_clock(SL_BUS_CLOCK_EUSART1);
  sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
  return true;
}

static void leave_motion_sleep(void)
{
  sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
  (void)sl_icm40627_spi_init();
  (void)sl_gpio_disable_interrupts(1u << motion_interrupt);
  (void)sl_icm40627_enable_interrupt(false, false);
  (void)sl_icm40627_write_register(ICM40627_REG_SMD_CONFIG, 0u);
  (void)sl_icm40627_accel_set_sample_rate(sl_accelODR_50Hz);
  uint8_t reason = 0u;
  (void)sl_icm40627_read_register(ICM40627_REG_INT_STATUS2, 1u, &reason);
  printf("POWER: motion wake=0x%x; verify wear, apps stay OFF; EM2=%lu\r\n",
         reason, (unsigned long)breathsense_em2_entries);
}

static void set_applications_active(bool active)
{
  breathsense_ai_set_capture_paused(!active);
  breathsense_sensor_set_paused(!active);
  breathsense_ble_set_paused(!active);
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

static void publish_status(uint8_t wear, bool imu_valid,
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
  sl_power_manager_subscribe_em_transition_event(&energy_handle, &energy_info);
  sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
  const bool imu_ready = start_imu();
  const bool irq_ready = imu_ready && configure_motion_irq();
  printf("POWER: WOM IRQ %s; boot apps OFF\r\n", irq_ready ? "ready" : "failed");
  printf("PENDANT: ICM-40627 %s\r\n", imu_ready ? "ready" : "unavailable");

  if (sl_sleeptimer_start_periodic_timer_ms(&timer, SAMPLE_PERIOD_MS,
                                           sample_due, NULL, 0u, 0u) != SL_STATUS_OK) {
    printf("PENDANT: timer failed\r\n");
    return;
  }

  breathsense_wear_policy_t policy = {0};
  bool sleeping = false;
  bool applications_active = false;
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
    const OS_FLAGS pending = OSFlagPend(&flags, sleeping ? MOTION_WAKE : SAMPLE_READY, 0u,
                     OS_OPT_PEND_FLAG_SET_ANY | OS_OPT_PEND_FLAG_CONSUME
                     | OS_OPT_PEND_BLOCKING, NULL, &err);
    if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
      continue;
    }

    if (sleeping && (pending & MOTION_WAKE)) {
      leave_motion_sleep();
      sleeping = false;
      breathsense_power_mode = 0u;
      breathsense_wear_policy_wake(&policy);
      samples = valid = motion = flat = 0u;
      previous_valid = false;
      (void)sl_sleeptimer_start_periodic_timer_ms(&timer, SAMPLE_PERIOD_MS,
                                                sample_due, NULL, 0u, 0u);
      continue;
    }

    float axes[3];
    const bool sample_valid = imu_ready
      && sl_icm40627_accel_read_data(axes) == SL_STATUS_OK
      && fabsf(axes[0]) + fabsf(axes[1]) + fabsf(axes[2]) > 0.25f;
    const float delta = sample_valid && previous_valid
      ? fabsf(axes[0] - previous[0]) + fabsf(axes[1] - previous[1])
        + fabsf(axes[2] - previous[2])
      : 0.0f;
    const bool is_flat = sample_valid && fabsf(axes[2]) >= 0.90f
                         && fabsf(axes[0]) < 0.40f && fabsf(axes[1]) < 0.40f;

    if (sample_valid) {
      ++valid;
      motion += delta >= MOTION_DELTA_G;
      flat += is_flat;
      for (unsigned axis = 0u; axis < 3u; ++axis) {
        previous[axis] = axes[axis];
      }
    }
    previous_valid = sample_valid;

    if (++samples < WINDOW_SAMPLES) {
      continue;
    }

    const bool imu_valid = valid >= FLAT_SAMPLES_MIN;
    const bool should_sleep = breathsense_wear_policy_update(&policy, imu_valid, motion, flat);
    if (policy.active != applications_active) {
      applications_active = policy.active;
      set_applications_active(applications_active);
      printf("POWER: applications %s (confirmed wear=%u)\r\n",
             applications_active ? "ON" : "OFF", policy.wear);
    }
    breathsense_power_mode = applications_active ? 1u : 0u;
    if (applications_active && ++voltage_windows >= 5u) {
      supply_mv = read_supply_mv();
      voltage_windows = 0u;
    }
    publish_status(policy.wear, imu_valid, supply_mv, motion, flat);
    printf("PENDANT: wear=%u vmcu_mv=%u imu=%u motion=%u flat=%u apps=%u\r\n",
           policy.wear, supply_mv, (unsigned)imu_valid, motion, flat, applications_active);
    if (should_sleep && irq_ready) {
      (void)sl_sleeptimer_stop_timer(&timer);
      if (arm_motion_sleep()) {
        sleeping = true;
        breathsense_power_mode = 2u;
        printf("POWER: SLEEP armed; WOM 12.5Hz; mic/AI/RHT/BLE/ADC OFF requested\r\n");
      } else {
        (void)sl_icm40627_write_register(ICM40627_REG_SMD_CONFIG, 0u);
        (void)sl_icm40627_accel_set_sample_rate(sl_accelODR_50Hz);
        breathsense_power_mode = 3u;
        printf("POWER: WOM verification failed; polling fallback\r\n");
        (void)sl_sleeptimer_start_periodic_timer_ms(&timer, SAMPLE_PERIOD_MS,
                                                  sample_due, NULL, 0u, 0u);
      }
    }
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
