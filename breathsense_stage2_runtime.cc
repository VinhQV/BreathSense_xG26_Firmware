/***************************************************************************//**
 * @file breathsense_stage2_runtime.cc
 * @brief Independent TFLite Micro runtime for dry/wet cough classification.
 ******************************************************************************/

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "breathsense_ai_product_config.h"
#include "breathsense_stage2_model_data.h"
#include "breathsense_stage2_runtime.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

constexpr std::size_t kStage2TensorArenaSize = 128u * 1024u;
constexpr int kStage2OperatorCount = 4;

constexpr float kExpectedInputScale = 0.003921568859368563f;
constexpr int32_t kExpectedInputZeroPoint = -128;
constexpr float kExpectedOutputScale = 0.09463200718164444f;
constexpr int32_t kExpectedOutputZeroPoint = -128;
constexpr float kQuantizationTolerance = 0.000001f;

/*
 * sigmoid(margin * kExpectedOutputScale), rounded to whole percent.
 *
 * For a two-logit softmax, the winning-class probability depends only on the
 * absolute logit difference. A small lookup table avoids expf() and its code
 * size/runtime cost on the MCU. Margins above the last entry saturate at 100%.
 * This estimate is model confidence, not a calibrated clinical probability.
 */
constexpr std::uint8_t kWinningProbabilityPercentByMargin[] = {
  50u, 52u, 55u, 57u, 59u, 62u, 64u, 66u,
  68u, 70u, 72u, 74u, 76u, 77u, 79u, 81u,
  82u, 83u, 85u, 86u, 87u, 88u, 89u, 90u,
  91u, 91u, 92u, 93u, 93u, 94u, 94u, 95u,
  95u, 96u, 96u, 96u, 97u, 97u, 97u, 98u,
  98u, 98u, 98u, 98u, 98u, 99u, 99u, 99u,
  99u, 99u, 99u, 99u, 99u, 99u, 99u, 99u,
  100u, 100u, 100u, 100u, 100u, 100u, 100u, 100u,
  100u
};

constexpr std::size_t kProbabilityTableSize =
  sizeof(kWinningProbabilityPercentByMargin)
  / sizeof(kWinningProbabilityPercentByMargin[0]);

static_assert(BS_AI_STAGE2_DRY_MAX_WET_PERCENT < 50u,
              "Stage 2 dry threshold must be below 50 percent");
static_assert(BS_AI_STAGE2_WET_MIN_WET_PERCENT > 50u,
              "Stage 2 wet threshold must be above 50 percent");
static_assert(BS_AI_STAGE2_DRY_MAX_WET_PERCENT
                < BS_AI_STAGE2_WET_MIN_WET_PERCENT,
              "Stage 2 decision band is invalid");
static_assert(BS_AI_STAGE2_WET_MIN_WET_PERCENT <= 100u,
              "Stage 2 wet threshold must not exceed 100 percent");

alignas(16)
static std::uint8_t stage2_tensor_arena[kStage2TensorArenaSize];

static tflite::MicroInterpreter *stage2_interpreter = nullptr;
static TfLiteTensor *stage2_input_tensor = nullptr;
static TfLiteTensor *stage2_output_tensor = nullptr;
static bool stage2_initialized = false;

static float absolute_float(float value)
{
  return (value < 0.0f) ? -value : value;
}

static std::uint8_t winning_probability_percent(std::uint8_t margin)
{
  const std::size_t index = static_cast<std::size_t>(margin);

  if (index >= kProbabilityTableSize) {
    return 100u;
  }

  return kWinningProbabilityPercentByMargin[index];
}

static bool add_required_operators(
  tflite::MicroMutableOpResolver<kStage2OperatorCount>& resolver)
{
  if (resolver.AddConv2D() != kTfLiteOk) {
    std::printf("Stage2 init FAIL: AddConv2D\r\n");
    return false;
  }

  if (resolver.AddMaxPool2D() != kTfLiteOk) {
    std::printf("Stage2 init FAIL: AddMaxPool2D\r\n");
    return false;
  }

  if (resolver.AddReduceMax() != kTfLiteOk) {
    std::printf("Stage2 init FAIL: AddReduceMax\r\n");
    return false;
  }

  if (resolver.AddFullyConnected() != kTfLiteOk) {
    std::printf("Stage2 init FAIL: AddFullyConnected\r\n");
    return false;
  }

  return true;
}

static bool input_tensor_is_valid(const TfLiteTensor *tensor)
{
  return tensor != nullptr
         && tensor->type == kTfLiteInt8
         && tensor->dims != nullptr
         && tensor->dims->size == 4
         && tensor->dims->data[0] == 1
         && tensor->dims->data[1] == 40
         && tensor->dims->data[2] == 50
         && tensor->dims->data[3] == 1
         && tensor->bytes == BREATHSENSE_STAGE2_INPUT_COUNT
         && absolute_float(tensor->params.scale - kExpectedInputScale)
              <= kQuantizationTolerance
         && tensor->params.zero_point == kExpectedInputZeroPoint;
}

static bool output_tensor_is_valid(const TfLiteTensor *tensor)
{
  return tensor != nullptr
         && tensor->type == kTfLiteInt8
         && tensor->dims != nullptr
         && tensor->dims->size == 2
         && tensor->dims->data[0] == 1
         && tensor->dims->data[1] == 2
         && tensor->bytes == BREATHSENSE_STAGE2_OUTPUT_COUNT
         && absolute_float(tensor->params.scale - kExpectedOutputScale)
              <= kQuantizationTolerance
         && tensor->params.zero_point == kExpectedOutputZeroPoint;
}

static void reset_result(breathsense_stage2_result_t *result)
{
  result->dry_score = 0;
  result->wet_score = 0;
  result->margin = 0u;
  result->wet_probability_percent = 50u;
  result->confidence_percent = 50u;
  result->decision = BREATHSENSE_STAGE2_ERROR;
  result->valid = false;
}

}  // namespace

extern "C" bool breathsense_stage2_runtime_init(void)
{
  if (stage2_initialized) {
    return true;
  }

  const tflite::Model *model =
    tflite::GetModel(g_breathsense_stage2_model_data);

  if (model == nullptr) {
    std::printf("Stage2 init FAIL: model pointer is null\r\n");
    return false;
  }

  if (model->version() != TFLITE_SCHEMA_VERSION) {
    std::printf("Stage2 init FAIL: schema model=%ld runtime=%d\r\n",
                static_cast<long>(model->version()),
                TFLITE_SCHEMA_VERSION);
    return false;
  }

  static tflite::MicroMutableOpResolver<kStage2OperatorCount> resolver;
  static bool resolver_ready = false;

  if (!resolver_ready) {
    if (!add_required_operators(resolver)) {
      return false;
    }

    resolver_ready = true;
  }

  static tflite::MicroInterpreter static_interpreter(
    model,
    resolver,
    stage2_tensor_arena,
    kStage2TensorArenaSize
  );

  stage2_interpreter = &static_interpreter;

  if (stage2_interpreter->AllocateTensors() != kTfLiteOk) {
    std::printf("Stage2 init FAIL: AllocateTensors, arena=%lu bytes\r\n",
                static_cast<unsigned long>(kStage2TensorArenaSize));
    stage2_interpreter = nullptr;
    return false;
  }

  stage2_input_tensor = stage2_interpreter->input(0);
  stage2_output_tensor = stage2_interpreter->output(0);

  if (!input_tensor_is_valid(stage2_input_tensor)) {
    std::printf(
      "Stage2 init FAIL: expected INT8 input [1,40,50,1], "
      "scale=%.9f zp=%ld\r\n",
      static_cast<double>(kExpectedInputScale),
      static_cast<long>(kExpectedInputZeroPoint));
    stage2_interpreter = nullptr;
    stage2_input_tensor = nullptr;
    stage2_output_tensor = nullptr;
    return false;
  }

  if (!output_tensor_is_valid(stage2_output_tensor)) {
    std::printf(
      "Stage2 init FAIL: expected INT8 output [1,2], "
      "scale=%.9f zp=%ld\r\n",
      static_cast<double>(kExpectedOutputScale),
      static_cast<long>(kExpectedOutputZeroPoint));
    stage2_interpreter = nullptr;
    stage2_input_tensor = nullptr;
    stage2_output_tensor = nullptr;
    return false;
  }

  stage2_initialized = true;

  std::printf(
    "Stage2 runtime ready: model=%lu bytes arena=%lu bytes "
    "input_scale=%.9f input_zp=%ld "
    "output_scale=%.9f output_zp=%ld\r\n",
    static_cast<unsigned long>(g_breathsense_stage2_model_data_len),
    static_cast<unsigned long>(kStage2TensorArenaSize),
    static_cast<double>(stage2_input_tensor->params.scale),
    static_cast<long>(stage2_input_tensor->params.zero_point),
    static_cast<double>(stage2_output_tensor->params.scale),
    static_cast<long>(stage2_output_tensor->params.zero_point));

  return true;
}

extern "C" bool breathsense_stage2_runtime_run(
  const int8_t *input_int8,
  size_t input_count,
  breathsense_stage2_result_t *result)
{
  if (input_int8 == nullptr
      || result == nullptr
      || input_count != BREATHSENSE_STAGE2_INPUT_COUNT) {
    return false;
  }

  reset_result(result);

  if (!breathsense_stage2_runtime_init()) {
    return false;
  }

  std::memcpy(stage2_input_tensor->data.int8,
              input_int8,
              BREATHSENSE_STAGE2_INPUT_COUNT);

  if (stage2_interpreter->Invoke() != kTfLiteOk) {
    std::printf("Stage2 inference FAIL: Invoke()\r\n");
    return false;
  }

  const int16_t dry =
    static_cast<int16_t>(stage2_output_tensor->data.int8[0]);

  const int16_t wet =
    static_cast<int16_t>(stage2_output_tensor->data.int8[1]);

  const int16_t signed_difference = dry - wet;
  const int16_t absolute_difference =
    (signed_difference < 0)
    ? -signed_difference
    : signed_difference;

  result->dry_score = static_cast<int8_t>(dry);
  result->wet_score = static_cast<int8_t>(wet);
  result->margin = static_cast<uint8_t>(absolute_difference);

  const std::uint8_t winning_probability =
    winning_probability_percent(result->margin);

  result->confidence_percent = winning_probability;
  result->wet_probability_percent =
    (wet >= dry)
    ? winning_probability
    : static_cast<std::uint8_t>(100u - winning_probability);

  result->valid = true;

  if (result->wet_probability_percent
      <= BS_AI_STAGE2_DRY_MAX_WET_PERCENT) {
    result->decision = BREATHSENSE_STAGE2_DRY;
  } else if (result->wet_probability_percent
             >= BS_AI_STAGE2_WET_MIN_WET_PERCENT) {
    result->decision = BREATHSENSE_STAGE2_WET;
  } else {
    result->decision = BREATHSENSE_STAGE2_REJECTED;
  }

  return true;
}
