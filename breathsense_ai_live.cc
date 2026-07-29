/***************************************************************************//**
 * @file breathsense_ai_live.cc
 * @brief Phan loai BreathSense Stage 1 + Stage 2 theo kieu event-driven.
 *
 * Toi uu RAM:
 * - Dung chung mot buffer dac trung INT8 2.000 byte cho Stage 1 va Stage 2.
 * - Dung lai cua so PCM cho ca hai model.
 * - Ring buffer gom 18 chunk, du 16 chunk cho mot cua so suy luan va con
 *   2 chunk du phong trong luc task dang sao chep du lieu.
 *
 * Thu tu xu ly bat buoc:
 *   Stage 1 MFCC -> Stage 1 Invoke -> neu COUGH thi Stage 2 Log-Mel -> Stage 2 Invoke
 *
 * Luong thoi gian thuc:
 * - Microphone callback sao chep chunk moi vao ring buffer.
 * - Callback post Event Flag AUDIO_READY.
 * - AI task block tai OSFlagPend(), khong polling moi 5 ms.
 * - Khi event ho duoc day vao queue, AI task post flag cho BLE TX task.
 ******************************************************************************/

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "breathsense_ble.h"
#include "breathsense_ai.h"
#include "breathsense_event.h"
#include "breathsense_indicator.h"
#include "breathsense_ai_product_config.h"
#include "breathsense_inference_runtime.h"
#include "breathsense_mfcc_config.h"
#include "breathsense_mfcc_runtime.h"
#include "breathsense_stage2_logmel_runtime.h"
#include "breathsense_stage2_runtime.h"

#include "em_assert.h"
#include "em_core.h"
#include "os.h"

#include "sl_board_control.h"
#include "sl_mic.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"

/*******************************************************************************
 ******************************* CAU HINH **************************************
 ******************************************************************************/

#define LIVE_AI_SAMPLE_RATE_HZ          16000u
#define LIVE_AI_CHANNEL_COUNT           1u

#define LIVE_AI_CHUNK_SAMPLES           1600u
#define LIVE_AI_WINDOW_CHUNKS           16u
#define LIVE_AI_STRIDE_CHUNKS           8u

/*
 * 18 chunk = 28.800 mau PCM16 = 57.600 byte.
 * Mot cua so model dung 16 chunk, con 2 chunk de tranh bi ghi de
 * trong luc task dang sao chep cua so suy luan.
 */
#define LIVE_AI_RING_CHUNKS             18u

#define LIVE_AI_STREAM_BUFFER_SAMPLES   (2u * LIVE_AI_CHUNK_SAMPLES)

#define LIVE_AI_TASK_STACK_SIZE         1024u
#define LIVE_AI_TASK_PRIORITY           21u

/*
 * Event Flag cua AI.
 * AUDIO_READY: microphone vua ghi xong mot chunk moi.
 * STREAM_ERROR: callback phat hien buffer hoac so frame khong hop le.
 */
#define LIVE_AI_FLAG_AUDIO_READY        ((OS_FLAGS)1u << 0u)
#define LIVE_AI_FLAG_STREAM_ERROR       ((OS_FLAGS)1u << 1u)
#define LIVE_AI_FLAG_ALL                \
  (LIVE_AI_FLAG_AUDIO_READY | LIVE_AI_FLAG_STREAM_ERROR)

static_assert(
  LIVE_AI_WINDOW_CHUNKS * LIVE_AI_CHUNK_SAMPLES
    == BS_AUDIO_WINDOW_SAMPLES,
  "Sliding window must contain exactly 25,600 samples");

static_assert(
  LIVE_AI_RING_CHUNKS > LIVE_AI_WINDOW_CHUNKS,
  "Ring buffer must be larger than one model window");

static_assert(
  BREATHSENSE_STAGE2_INPUT_COUNT
    == BREATHSENSE_STAGE2_LOGMEL_ELEMENT_COUNT,
  "Stage 2 runtime and Log-Mel frontend sizes must match");

/*******************************************************************************
 ******************************* BO NHO TINH ***********************************
 ******************************************************************************/

static OS_TCB live_ai_task_tcb;
static CPU_STK live_ai_task_stack[LIVE_AI_TASK_STACK_SIZE];
static CPU_CHAR live_ai_task_name[] = "BreathSense live AI";

/*
 * Flag group phai duoc tao truoc khi task microphone bat dau streaming.
 */
static OS_FLAG_GRP live_ai_flag_group;
static CPU_CHAR live_ai_flag_group_name[] = "BreathSense AI Flags";
static bool live_ai_flag_group_ready = false;
static volatile uint32_t live_ai_flag_post_error_count = 0u;

alignas(4) static int16_t
  microphone_stream_buffer[LIVE_AI_STREAM_BUFFER_SAMPLES];

alignas(16) static int16_t
  audio_ring[LIVE_AI_RING_CHUNKS * LIVE_AI_CHUNK_SAMPLES];

/* Dung chung cho Stage 1 va Stage 2, khong tao them buffer PCM 25.600 mau. */
alignas(16) static int16_t
  inference_pcm16[BS_AUDIO_WINDOW_SAMPLES];

/*
 * Stage 1 va Stage 2 chay noi tiep nhau.
 * Chi cap phat mot buffer theo kich thuoc lon hon va chi ghi de sau khi
 * Stage 1 Invoke da ket thuc.
 */
constexpr std::size_t kSharedModelInputCount =
  (BREATHSENSE_MODEL_INPUT_COUNT
   > BREATHSENSE_STAGE2_LOGMEL_ELEMENT_COUNT)
    ? BREATHSENSE_MODEL_INPUT_COUNT
    : BREATHSENSE_STAGE2_LOGMEL_ELEMENT_COUNT;

alignas(16) static int8_t
  shared_model_input[kSharedModelInputCount];

static breathsense_ai_result_t latest_result;
static uint16_t inference_sequence = 0u;

static volatile uint32_t total_chunks_written = 0u;
static volatile bool stream_callback_error = false;

/* Trang thai on dinh cua Stage 1 de chong nhay nhan va dem trung. */
static uint8_t stable_class = BREATHSENSE_AI_OTHER;
static bool stable_class_valid = false;
static uint8_t candidate_class = BREATHSENSE_AI_OTHER;
static uint8_t candidate_hits = 0u;

/*******************************************************************************
 ***************************** HAM NOI BO *************************************
 ******************************************************************************/

static void live_ai_post_flag(OS_FLAGS flags)
{
  RTOS_ERR err;

  if (!live_ai_flag_group_ready) {
    return;
  }

  /*
   * Ham nay co the duoc goi tu microphone callback.
   * Khong printf, khong MFCC va khong inference tai day.
   */
  (void)OSFlagPost(&live_ai_flag_group,
                   flags,
                   OS_OPT_POST_FLAG_SET,
                   &err);

  if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
    ++live_ai_flag_post_error_count;
  }
}

static const char *class_name_upper(uint8_t class_id)
{
  static const char *const names[4] = {
    "COUGH",
    "OTHER",
    "SNEEZE",
    "SPEECH"
  };

  return (class_id < 4u) ? names[class_id] : "INVALID";
}

static const char *stage2_name_upper(
  breathsense_stage2_decision_t decision)
{
  switch (decision) {
    case BREATHSENSE_STAGE2_DRY:
      return "DRY";

    case BREATHSENSE_STAGE2_WET:
      return "WET";

    case BREATHSENSE_STAGE2_REJECTED:
      return "REJECTED";

    case BREATHSENSE_STAGE2_ERROR:
    default:
      return "ERROR";
  }
}

static uint8_t find_top1(const float probabilities[4])
{
  uint8_t top1 = 0u;

  for (uint8_t i = 1u; i < 4u; ++i) {
    if (probabilities[i] > probabilities[top1]) {
      top1 = i;
    }
  }

  return top1;
}


static uint8_t probability_to_percent(float probability)
{
  if (probability <= 0.0f) {
    return 0u;
  }

  if (probability >= 1.0f) {
    return 100u;
  }

  return static_cast<uint8_t>(probability * 100.0f + 0.5f);
}

static uint32_t uptime_seconds(void)
{
  const uint32_t uptime_ms =
    sl_sleeptimer_tick_to_ms(
      sl_sleeptimer_get_tick_count());

  return uptime_ms / 1000u;
}

static float minimum_probability(uint8_t class_id)
{
  switch (class_id) {
    case BREATHSENSE_AI_COUGH:
      return BS_AI_COUGH_MIN_PROBABILITY;

    case BREATHSENSE_AI_SNEEZE:
      return BS_AI_SNEEZE_MIN_PROBABILITY;

    case BREATHSENSE_AI_SPEECH:
      return BS_AI_SPEECH_MIN_PROBABILITY;

    case BREATHSENSE_AI_OTHER:
    default:
      return BS_AI_OTHER_MIN_PROBABILITY;
  }
}

static bool strong_single_window(uint8_t class_id,
                                 float probability)
{
  switch (class_id) {
    case BREATHSENSE_AI_COUGH:
      return probability >= BS_AI_COUGH_STRONG_PROBABILITY;

    case BREATHSENSE_AI_SNEEZE:
      return probability >= BS_AI_SNEEZE_STRONG_PROBABILITY;

    case BREATHSENSE_AI_SPEECH:
      return probability >= BS_AI_SPEECH_STRONG_PROBABILITY;

    case BREATHSENSE_AI_OTHER:
    default:
      return false;
  }
}

static uint8_t required_hits(uint8_t class_id)
{
  return (class_id == BREATHSENSE_AI_OTHER)
         ? BS_AI_REQUIRED_HITS_OTHER
         : BS_AI_REQUIRED_HITS_EVENT;
}

static void calculate_audio_level(const int16_t *samples,
                                  std::size_t sample_count,
                                  uint32_t *peak,
                                  uint32_t *mean_absolute)
{
  uint32_t local_peak = 0u;
  uint64_t absolute_sum = 0u;

  for (std::size_t i = 0u; i < sample_count; ++i) {
    const int32_t sample = samples[i];

    const uint32_t absolute =
      (sample < 0)
      ? static_cast<uint32_t>(-sample)
      : static_cast<uint32_t>(sample);

    if (absolute > local_peak) {
      local_peak = absolute;
    }

    absolute_sum += absolute;
  }

  *peak = local_peak;
  *mean_absolute =
    static_cast<uint32_t>(absolute_sum / sample_count);
}

static bool audio_is_active(uint32_t peak,
                            uint32_t mean_absolute)
{
  return peak >= BS_AI_AUDIO_GATE_PEAK
         || mean_absolute >= BS_AI_AUDIO_GATE_MEAN_ABS;
}

static void publish_result(uint8_t class_id,
                           float confidence)
{
  breathsense_ai_result_t result;

  result.class_id =
    static_cast<breathsense_ai_class_t>(class_id);

  float confidence_percent = confidence * 100.0f;

  if (confidence_percent < 0.0f) {
    confidence_percent = 0.0f;
  } else if (confidence_percent > 100.0f) {
    confidence_percent = 100.0f;
  }

  result.confidence_percent =
    static_cast<uint8_t>(confidence_percent + 0.5f);

  result.sequence = ++inference_sequence;

  result.timestamp_ms =
    sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());

  result.valid = true;

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();
    latest_result = result;
    CORE_EXIT_ATOMIC();
  }
}

static uint8_t update_stable_class(
  const float probabilities[4],
  uint8_t raw_top1,
  bool active_audio,
  bool *class_changed)
{
  uint8_t proposed_class = raw_top1;
  float proposed_probability = probabilities[raw_top1];

  *class_changed = false;

  if (!active_audio
      || proposed_probability
         < minimum_probability(proposed_class)) {
    proposed_class = BREATHSENSE_AI_OTHER;
    proposed_probability =
      probabilities[BREATHSENSE_AI_OTHER];
  }

  if (proposed_class == candidate_class) {
    if (candidate_hits < 255u) {
      ++candidate_hits;
    }
  } else {
    candidate_class = proposed_class;
    candidate_hits = 1u;
  }

  const bool confirmed =
    strong_single_window(proposed_class,
                         proposed_probability)
    || candidate_hits >= required_hits(proposed_class);

  if (confirmed
      && (!stable_class_valid
          || proposed_class != stable_class)) {
    stable_class = proposed_class;
    stable_class_valid = true;
    *class_changed = true;
  }

  if (!stable_class_valid) {
    stable_class = BREATHSENSE_AI_OTHER;
  }

  return stable_class;
}

static void microphone_stream_callback(const void *buffer,
                                       uint32_t n_frames)
{
  /*
   * Callback chi duoc lam cong viec ngan:
   * - kiem tra tham so;
   * - sao chep mot chunk PCM;
   * - cap nhat counter;
   * - post Event Flag danh thuc AI task.
   */
  if ((buffer == nullptr)
      || (n_frames != LIVE_AI_CHUNK_SAMPLES)) {
    stream_callback_error = true;
    live_ai_post_flag(LIVE_AI_FLAG_STREAM_ERROR);
    return;
  }

  const uint32_t chunk_id = total_chunks_written;
  const uint32_t ring_slot =
    chunk_id % LIVE_AI_RING_CHUNKS;

  int16_t *destination =
    &audio_ring[ring_slot * LIVE_AI_CHUNK_SAMPLES];

  const int16_t *source =
    static_cast<const int16_t *>(buffer);

  std::memcpy(destination,
              source,
              LIVE_AI_CHUNK_SAMPLES * sizeof(int16_t));

  /*
   * Phai cap nhat counter truoc, sau do moi post flag.
   * Khi AI task thuc day, chunk moi da san sang trong ring buffer.
   */
  total_chunks_written = chunk_id + 1u;
  live_ai_post_flag(LIVE_AI_FLAG_AUDIO_READY);
}

static bool snapshot_window(uint32_t end_chunk)
{
  const uint32_t first_chunk =
    end_chunk - LIVE_AI_WINDOW_CHUNKS;

  for (uint32_t window_chunk = 0u;
       window_chunk < LIVE_AI_WINDOW_CHUNKS;
       ++window_chunk) {
    const uint32_t source_chunk =
      first_chunk + window_chunk;

    const uint32_t ring_slot =
      source_chunk % LIVE_AI_RING_CHUNKS;

    const int16_t *source =
      &audio_ring[ring_slot * LIVE_AI_CHUNK_SAMPLES];

    int16_t *destination =
      &inference_pcm16[window_chunk * LIVE_AI_CHUNK_SAMPLES];

    std::memcpy(destination,
                source,
                LIVE_AI_CHUNK_SAMPLES * sizeof(int16_t));
  }

  const uint32_t chunks_arrived_during_copy =
    total_chunks_written - end_chunk;

  return chunks_arrived_during_copy
         < (LIVE_AI_RING_CHUNKS - LIVE_AI_WINDOW_CHUNKS);
}

static void run_stage2_and_queue_cough(
  uint8_t stage1_confidence_percent)
{
  /*
   * Stage 1 Invoke da ket thuc truoc khi vao ham nay.
   * Vi vay Stage 2 co the ghi de shared_model_input an toan.
   */
  if (!breathsense_stage2_logmel_runtime_process_pcm16(
        inference_pcm16,
        BS_AUDIO_WINDOW_SAMPLES,
        shared_model_input,
        BREATHSENSE_STAGE2_LOGMEL_ELEMENT_COUNT)) {
    std::printf("AI ERROR: Stage 2 Log-Mel processing\r\n");
    return;
  }

  breathsense_stage2_result_t stage2_result;

  if (!breathsense_stage2_runtime_run(
        shared_model_input,
        BREATHSENSE_STAGE2_INPUT_COUNT,
        &stage2_result)) {
    std::printf("AI ERROR: Stage 2 inference\r\n");
    return;
  }

  if (!stage2_result.valid) {
    std::printf("AI ERROR: Stage 2 result invalid\r\n");
    return;
  }

  breathsense_cough_type_t cough_type =
    BREATHSENSE_COUGH_TYPE_UNKNOWN;

  switch (stage2_result.decision) {
    case BREATHSENSE_STAGE2_DRY:
      cough_type = BREATHSENSE_COUGH_TYPE_DRY;
      break;

    case BREATHSENSE_STAGE2_WET:
      cough_type = BREATHSENSE_COUGH_TYPE_WET;
      break;

    case BREATHSENSE_STAGE2_REJECTED:
    case BREATHSENSE_STAGE2_ERROR:
    default:
      cough_type = BREATHSENSE_COUGH_TYPE_UNKNOWN;
      break;
  }

  std::printf("AI COUGH TYPE: %s dry=%d wet=%d margin=%u\r\n",
              stage2_name_upper(stage2_result.decision),
              static_cast<int>(stage2_result.dry_score),
              static_cast<int>(stage2_result.wet_score),
              static_cast<unsigned int>(stage2_result.margin));

  /*
   * Stage 2 hien chi tra ve raw score va raw margin.
   * Chua co cong thuc chuan de doi sang confidence 0..100,
   * vi vay tam thoi giu gia tri nay bang 0.
   */
  const uint8_t stage2_confidence_percent = 0u;

  const uint16_t assigned_counter =
    breathsense_event_next_counter();

  const bool queued =
    breathsense_event_push_cough(
      uptime_seconds(),
      false,
      cough_type,
      stage1_confidence_percent,
      stage2_confidence_percent);

  if (queued) {
    /*
     * Queue giu payload, Event Flag chi danh thuc BLE TX task.
     * Phai push queue thanh cong truoc khi post flag.
     */
    breathsense_ble_notify_cough_ready();

    std::printf(
      "AI EVENT QUEUED: counter=%u type=%u s1=%u s2=%u queue=%u\r\n",
      static_cast<unsigned int>(assigned_counter),
      static_cast<unsigned int>(cough_type),
      static_cast<unsigned int>(stage1_confidence_percent),
      static_cast<unsigned int>(stage2_confidence_percent),
      static_cast<unsigned int>(breathsense_event_count()));
  } else {
    std::printf(
      "AI EVENT DROP: queue=%u dropped=%lu\r\n",
      static_cast<unsigned int>(breathsense_event_count()),
      static_cast<unsigned long>(breathsense_event_dropped_count()));
  }
}

static void live_ai_task(void *arg)
{
  RTOS_ERR err;
  sl_status_t status;

  (void)arg;

  std::printf("BreathSense AI product mode started\r\n");

  status =
    sl_board_enable_sensor(SL_BOARD_SENSOR_MICROPHONE);

  if (status != SL_STATUS_OK) {
    std::printf("AI ERROR: microphone power 0x%08lx\r\n",
                static_cast<unsigned long>(status));
    return;
  }

  OSTimeDlyHMSM(0,
                0,
                0,
                50,
                OS_OPT_TIME_DLY | OS_OPT_TIME_HMSM_STRICT,
                &err);

  if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
    std::printf("AI ERROR: startup delay %ld\r\n",
                static_cast<long>(RTOS_ERR_CODE_GET(err)));
    return;
  }

  status =
    sl_mic_init(LIVE_AI_SAMPLE_RATE_HZ,
                LIVE_AI_CHANNEL_COUNT);

  if (status != SL_STATUS_OK) {
    std::printf("AI ERROR: microphone init 0x%08lx\r\n",
                static_cast<unsigned long>(status));
    return;
  }

  if (!breathsense_mfcc_runtime_init()) {
    std::printf("AI ERROR: MFCC init\r\n");
    return;
  }

  if (!breathsense_inference_runtime_init()) {
    std::printf("AI ERROR: Stage 1 inference init\r\n");
    return;
  }

  if (!breathsense_stage2_logmel_runtime_init()) {
    std::printf("AI ERROR: Stage 2 Log-Mel init\r\n");
    return;
  }

  if (!breathsense_stage2_runtime_init()) {
    std::printf("AI ERROR: Stage 2 inference init\r\n");
    return;
  }

  total_chunks_written = 0u;
  stream_callback_error = false;

  stable_class = BREATHSENSE_AI_OTHER;
  stable_class_valid = false;
  candidate_class = BREATHSENSE_AI_OTHER;
  candidate_hits = 0u;

  status =
    sl_mic_start_streaming(microphone_stream_buffer,
                           LIVE_AI_CHUNK_SAMPLES,
                           microphone_stream_callback);

  if (status != SL_STATUS_OK) {
    std::printf("AI ERROR: streaming start 0x%08lx\r\n",
                static_cast<unsigned long>(status));
    return;
  }

  std::printf("AI READY: Stage1=COUGH,OTHER,SNEEZE,SPEECH "
              "Stage2=DRY,WET\r\n");

  uint32_t last_processed_end_chunk = 0u;
  uint32_t previous_log_tick =
    sl_sleeptimer_get_tick_count();

  while (1) {
    if (stream_callback_error) {
      (void)sl_mic_stop();
      std::printf("AI ERROR: microphone callback\r\n");
      return;
    }

    const uint32_t available_chunks =
      total_chunks_written;

    const bool first_window_ready =
      (last_processed_end_chunk == 0u)
      && (available_chunks >= LIVE_AI_WINDOW_CHUNKS);

    const bool stride_ready =
      (last_processed_end_chunk != 0u)
      && ((available_chunks - last_processed_end_chunk)
          >= LIVE_AI_STRIDE_CHUNKS);

    if (!first_window_ready && !stride_ready) {
      /*
       * Khong polling va khong delay 5 ms.
       * Task ngu tai day cho den khi microphone callback post flag.
       */
      const OS_FLAGS flags =
        OSFlagPend(&live_ai_flag_group,
                   LIVE_AI_FLAG_ALL,
                   0u,
                   OS_OPT_PEND_FLAG_SET_ANY
                   | OS_OPT_PEND_FLAG_CONSUME
                   | OS_OPT_PEND_BLOCKING,
                   DEF_NULL,
                   &err);

      if (RTOS_ERR_CODE_GET(err) != RTOS_ERR_NONE) {
        std::printf("AI ERROR: flag pend %ld\r\n",
                    static_cast<long>(RTOS_ERR_CODE_GET(err)));
        continue;
      }

      if ((flags & LIVE_AI_FLAG_STREAM_ERROR) != 0u) {
        stream_callback_error = true;
      }

      /*
       * Quay lai dau vong lap de doc total_chunks_written moi nhat.
       */
      continue;
    }

    const uint32_t end_chunk = available_chunks;

    if (!snapshot_window(end_chunk)) {
      continue;
    }

    last_processed_end_chunk = end_chunk;

    uint32_t peak = 0u;
    uint32_t mean_absolute = 0u;

    calculate_audio_level(inference_pcm16,
                          BS_AUDIO_WINDOW_SAMPLES,
                          &peak,
                          &mean_absolute);

    const uint32_t processing_start_tick =
      sl_sleeptimer_get_tick_count();

    /* Stage 1 ghi MFCC vao buffer INT8 dung chung. */
    if (!breathsense_mfcc_runtime_process_pcm16(
          inference_pcm16,
          BS_AUDIO_WINDOW_SAMPLES,
          shared_model_input,
          BREATHSENSE_MODEL_INPUT_COUNT)) {
      (void)sl_mic_stop();
      std::printf("AI ERROR: MFCC processing\r\n");
      return;
    }

    int8_t output_int8[BREATHSENSE_MODEL_OUTPUT_COUNT];
    float probabilities[BREATHSENSE_MODEL_OUTPUT_COUNT];

    /* Stage 1 phai Invoke xong truoc khi Stage 2 ghi de buffer dung chung. */
    if (!breathsense_inference_runtime_run(
          shared_model_input,
          BREATHSENSE_MODEL_INPUT_COUNT,
          output_int8,
          probabilities)) {
      (void)sl_mic_stop();
      std::printf("AI ERROR: Stage 1 inference\r\n");
      return;
    }

    const uint8_t raw_top1 = find_top1(probabilities);
    const bool active_audio =
      audio_is_active(peak, mean_absolute);

    bool class_changed = false;

    const uint8_t confirmed_class =
      update_stable_class(probabilities,
                          raw_top1,
                          active_audio,
                          &class_changed);

    publish_result(confirmed_class,
                   probabilities[confirmed_class]);

    if (class_changed) {
      breathsense_indicator_set_class(
        static_cast<breathsense_ai_class_t>(confirmed_class)
      );

      std::printf("AI DETECTED: %s\r\n",
                  class_name_upper(confirmed_class));
    }

    /*
     * Chi tao mot event khi nhan on dinh cua Stage 1 chuyen sang COUGH.
     * Cac cua so COUGH chong lap lien tiep khong tao event trung.
     * Khi nhan quay ve OTHER, he thong moi cho phep tao event ho tiep theo.
     */
    const bool new_cough_event =
      class_changed
      && confirmed_class == BREATHSENSE_AI_COUGH;

    if (new_cough_event) {
      const uint8_t stage1_confidence_percent =
        probability_to_percent(
          probabilities[BREATHSENSE_AI_COUGH]);

      run_stage2_and_queue_cough(
        stage1_confidence_percent);
    }

    const uint32_t processing_ms =
      sl_sleeptimer_tick_to_ms(
        sl_sleeptimer_get_tick_count() - processing_start_tick);

#if BS_AI_DEBUG_RAW_LOGS
    const uint32_t current_log_tick =
      sl_sleeptimer_get_tick_count();

    const uint32_t update_ms =
      sl_sleeptimer_tick_to_ms(
        current_log_tick - previous_log_tick);

    previous_log_tick = current_log_tick;

    std::printf(
      "AI DEBUG: cough=%.2f%% other=%.2f%% "
      "sneeze=%.2f%% speech=%.2f%% "
      "raw=%s stable=%s active=%u peak=%lu mean_abs=%lu "
      "process=%lu update=%lu\r\n",
      static_cast<double>(probabilities[0] * 100.0f),
      static_cast<double>(probabilities[1] * 100.0f),
      static_cast<double>(probabilities[2] * 100.0f),
      static_cast<double>(probabilities[3] * 100.0f),
      class_name_upper(raw_top1),
      class_name_upper(confirmed_class),
      active_audio ? 1u : 0u,
      static_cast<unsigned long>(peak),
      static_cast<unsigned long>(mean_absolute),
      static_cast<unsigned long>(processing_ms),
      static_cast<unsigned long>(update_ms));
#else
    (void)processing_ms;
    (void)previous_log_tick;
#endif
  }
}

/*******************************************************************************
 ***************************** GIAO DIEN AI ***********************************
 ******************************************************************************/

extern "C" void breathsense_ai_init(void)
{
  RTOS_ERR err;

  breathsense_indicator_init();

  /*
   * Tao Event Flag Group truoc khi tao AI task.
   * Microphone callback chi bat dau sau khi task khoi tao streaming.
   */
  OSFlagCreate(&live_ai_flag_group,
               live_ai_flag_group_name,
               0u,
               &err);

  EFM_ASSERT(RTOS_ERR_CODE_GET(err) == RTOS_ERR_NONE);

  live_ai_flag_group_ready = true;
  live_ai_flag_post_error_count = 0u;

  {
    CORE_DECLARE_IRQ_STATE;

    CORE_ENTER_ATOMIC();
    latest_result.class_id = BREATHSENSE_AI_OTHER;
    latest_result.confidence_percent = 0u;
    latest_result.sequence = 0u;
    latest_result.timestamp_ms = 0u;
    latest_result.valid = false;
    CORE_EXIT_ATOMIC();
  }

  OSTaskCreate(&live_ai_task_tcb,
               live_ai_task_name,
               live_ai_task,
               DEF_NULL,
               LIVE_AI_TASK_PRIORITY,
               &live_ai_task_stack[0],
               LIVE_AI_TASK_STACK_SIZE / 10u,
               LIVE_AI_TASK_STACK_SIZE,
               0u,
               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CLR,
               &err);

  EFM_ASSERT(RTOS_ERR_CODE_GET(err) == RTOS_ERR_NONE);
}

extern "C" bool breathsense_ai_get_latest(
  breathsense_ai_result_t *result)
{
  if (result == nullptr) {
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