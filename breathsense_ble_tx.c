/***************************************************************************//**
 * @file breathsense_ble_tx.c
 * @brief Task gui BLE event-driven bang Micrium OS Event Flags.
 *
 * Nguyen tac:
 * - Event queue giu cac cough event.
 * - Bo nho cache giu mau nhiet do/do am moi nhat.
 * - Event flag chi dung de bao rang co viec can xu ly.
 * - BLE TX task block tai OSFlagPend() khi khong co su kien.
 * - Khong dung polling va khong dung delay dinh ky trong BLE TX task.
 ******************************************************************************/

#include "breathsense_ble_tx.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "breathsense_ble_advertising.h"
#include "breathsense_event.h"
#include "em_assert.h"
#include "em_core.h"
#include "os.h"

/*******************************************************************************
 ******************************* CAU HINH TASK *********************************
 ******************************************************************************/

#define BLE_TX_TASK_STACK_SIZE          384u
#define BLE_TX_TASK_PRIORITY            26u

/*
 * Moi bit dai dien cho mot loai cong viec.
 */
#define BLE_TX_FLAG_COUGH_READY         ((OS_FLAGS)1u << 0u)
#define BLE_TX_FLAG_ENV_READY           ((OS_FLAGS)1u << 1u)
#define BLE_TX_FLAG_STATE_CHANGED       ((OS_FLAGS)1u << 2u)

#define BLE_TX_FLAG_ALL                 \
  (BLE_TX_FLAG_COUGH_READY            | \
   BLE_TX_FLAG_ENV_READY              | \
   BLE_TX_FLAG_STATE_CHANGED)

/*******************************************************************************
 ******************************* BO NHO TINH ***********************************
 ******************************************************************************/

static OS_TCB ble_tx_task_tcb;
static CPU_STK ble_tx_task_stack[BLE_TX_TASK_STACK_SIZE];
static CPU_CHAR ble_tx_task_name[] = "BreathSense BLE TX";

static OS_FLAG_GRP ble_tx_flag_group;
static CPU_CHAR ble_tx_flag_group_name[] = "BreathSense BLE TX Flags";

static bool ble_tx_initialized = false;
static bool ble_tx_flag_group_ready = false;

/*
 * Dem loi post flag de debug.
 * Khong printf trong ham post flag vi ham nay nam tren duong thoi gian thuc.
 */
static uint32_t ble_tx_flag_post_error_count = 0u;

/*
 * Cache chi giu mau moi nhat cua Si7021.
 *
 * generation tang moi khi sensor cap nhat mau moi.
 * Sau khi gui xong, task chi xoa pending neu generation khong doi.
 * Cach nay tranh xoa nham mot mau moi vua duoc sensor ghi vao.
 */
static int32_t environment_temperature_mc = 0;
static uint32_t environment_humidity_x1000 = 0u;
static uint32_t environment_generation = 0u;
static bool environment_pending = false;

typedef struct {
  int32_t temperature_mc;
  uint32_t humidity_x1000;
  uint32_t generation;
  bool valid;
} environment_snapshot_t;

/*******************************************************************************
 ***************************** HAM NOI BO - FLAG *******************************
 ******************************************************************************/

static void ble_tx_post_flag(OS_FLAGS flags)
{
  RTOS_ERR err;

  if (!ble_tx_flag_group_ready) {
    return;
  }

  (void)OSFlagPost(&ble_tx_flag_group,
                   flags,
                   OS_OPT_POST_FLAG_SET,
                   &err);

  if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();
    ++ble_tx_flag_post_error_count;
    CORE_EXIT_ATOMIC();
  }
}

/*
 * Callback nay duoc BLE module goi khi:
 * - gateway ket noi;
 * - gateway bat/tat notification;
 * - gateway ngat ket noi.
 *
 * Callback chi post flag, khong encode payload va khong gui BLE tai day.
 */
static void ble_tx_on_ble_state_changed(void)
{
  ble_tx_post_flag(BLE_TX_FLAG_STATE_CHANGED);
}

/*******************************************************************************
 ************************* HAM NOI BO - ENV CACHE ******************************
 ******************************************************************************/

static bool ble_tx_copy_environment_snapshot(
  environment_snapshot_t *snapshot)
{
  if (snapshot == NULL) {
    return false;
  }

  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();

  snapshot->temperature_mc = environment_temperature_mc;
  snapshot->humidity_x1000 = environment_humidity_x1000;
  snapshot->generation = environment_generation;
  snapshot->valid = environment_pending;

  CORE_EXIT_ATOMIC();

  return snapshot->valid;
}

static void ble_tx_mark_environment_sent(uint32_t sent_generation)
{
  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();

  /*
   * Chi xoa pending neu khong co mau moi nao den trong luc dang gui.
   */
  if (environment_generation == sent_generation) {
    environment_pending = false;
  }

  CORE_EXIT_ATOMIC();
}

/*******************************************************************************
 ************************ HAM NOI BO - GUI COUGH *******************************
 ******************************************************************************/

static void ble_tx_process_cough_events(void)
{
  /*
   * Xu ly lien tuc cho den khi:
   * - queue rong;
   * - BLE chua san sang;
   * - hoac lan gui hien tai bi loi.
   */
  while (breathsense_ble_advertising_is_ready()) {
    breathsense_event_t event;

    if (!breathsense_event_peek(&event)) {
      return;
    }

    const bool sent =
      breathsense_ble_advertising_send_cough_event(
        (uint8_t)event.cough_type,
        event.event_counter);

    if (!sent) {
      /*
       * Khong pop event khi gui loi.
       * Event van nam trong queue va se duoc thu lai khi co flag moi.
       */
      return;
    }

    if (!breathsense_event_pop()) {
      /*
       * Truong hop nay khong nen xay ra neu chi co mot consumer.
       */
      printf("BLE TX ERROR: event pop failed\r\n");
      return;
    }

    printf("BLE EVENT SENT: counter=%u type=%u remaining=%u\r\n",
           event.event_counter,
           event.cough_type,
           breathsense_event_count());
  }
}

/*******************************************************************************
 ********************** HAM NOI BO - GUI ENVIRONMENT **************************
 ******************************************************************************/

static void ble_tx_process_environment(void)
{
  environment_snapshot_t snapshot;

  if (!breathsense_ble_advertising_is_environment_ready()) {
    return;
  }

  if (!ble_tx_copy_environment_snapshot(&snapshot)) {
    return;
  }

  const bool sent =
    breathsense_ble_advertising_send_environment(
      snapshot.temperature_mc,
      snapshot.humidity_x1000);

  if (!sent) {
    /*
     * Khong xoa pending khi gui loi.
     */
    return;
  }

  ble_tx_mark_environment_sent(snapshot.generation);
}

/*******************************************************************************
 ******************************* BLE TX TASK **********************************
 ******************************************************************************/

static void ble_tx_task(void *arg)
{
  RTOS_ERR err;

  (void)arg;

  printf("BLE TX task started: event-driven OSFlagPend\r\n");

  while (DEF_TRUE) {
    OS_FLAGS flags;

    /*
     * timeout = 0:
     * task cho vo han den khi co it nhat mot flag duoc post.
     *
     * SET_ANY:
     * chi can mot trong cac flag xuat hien la task duoc danh thuc.
     *
     * CONSUME:
     * cac flag lam task thuc day se duoc tu dong clear.
     *
     * BLOCKING:
     * task ngu, khong chiem CPU de kiem tra lien tuc.
     */
    flags = OSFlagPend(&ble_tx_flag_group,
                       BLE_TX_FLAG_ALL,
                       0u,
                       OS_OPT_PEND_FLAG_SET_ANY
                       | OS_OPT_PEND_FLAG_CONSUME
                       | OS_OPT_PEND_BLOCKING,
                       DEF_NULL,
                       &err);

    if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
      /*
       * Task RTOS khong return khi pend loi.
       * Bo qua lan loi va tiep tuc cho su kien tiep theo.
       */
      continue;
    }

    /*
     * Khi BLE state thay doi, can thu lai ca queue cough va mau sensor.
     */
    if ((flags & (BLE_TX_FLAG_COUGH_READY
                  | BLE_TX_FLAG_STATE_CHANGED)) != 0u) {
      ble_tx_process_cough_events();
    }

    if ((flags & (BLE_TX_FLAG_ENV_READY
                  | BLE_TX_FLAG_STATE_CHANGED)) != 0u) {
      ble_tx_process_environment();
    }
  }
}

/*******************************************************************************
 ***************************** GIAO DIEN CONG KHAI ******************************
 ******************************************************************************/

void breathsense_ble_tx_init(void)
{
  RTOS_ERR err;

  if (ble_tx_initialized) {
    return;
  }

  OSFlagCreate(&ble_tx_flag_group,
               ble_tx_flag_group_name,
               0u,
               &err);

  EFM_ASSERT(RTOS_ERR_CODE_GET(err) == RTOS_ERR_NONE);

  ble_tx_flag_group_ready = true;

  /*
   * BLE module se chi goi callback nhe nay khi state thay doi.
   */
  breathsense_ble_advertising_set_state_changed_callback(
    ble_tx_on_ble_state_changed);

  OSTaskCreate(&ble_tx_task_tcb,
               ble_tx_task_name,
               ble_tx_task,
               DEF_NULL,
               BLE_TX_TASK_PRIORITY,
               &ble_tx_task_stack[0],
               BLE_TX_TASK_STACK_SIZE / 10u,
               BLE_TX_TASK_STACK_SIZE,
               0u,
               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CLR,
               &err);

  EFM_ASSERT(RTOS_ERR_CODE_GET(err) == RTOS_ERR_NONE);

  ble_tx_initialized = true;

  /*
   * Danh thuc task mot lan de dong bo state ban dau.
   */
  ble_tx_post_flag(BLE_TX_FLAG_STATE_CHANGED);
}

void breathsense_ble_tx_notify_cough_ready(void)
{
  ble_tx_post_flag(BLE_TX_FLAG_COUGH_READY);
}

void breathsense_ble_tx_publish_environment(
  int32_t temperature_mc,
  uint32_t humidity_x1000)
{
  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();

    environment_temperature_mc = temperature_mc;
    environment_humidity_x1000 = humidity_x1000;
    ++environment_generation;
    environment_pending = true;

    CORE_EXIT_ATOMIC();
  }

  /*
   * Phai ghi cache truoc, sau do moi post flag.
   */
  ble_tx_post_flag(BLE_TX_FLAG_ENV_READY);
}
