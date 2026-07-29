/***************************************************************************//**
 * @file breathsense_stage2_runtime.cc
 * @brief Independent TFLite Micro runtime for dry/wet cough classification.
 ******************************************************************************/

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

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
  result->valid = true;

  if (absolute_difference
      < static_cast<int16_t>(BREATHSENSE_STAGE2_MARGIN_THRESHOLD)) {
    result->decision = BREATHSENSE_STAGE2_REJECTED;
  } else if (dry > wet) {
    result->decision = BREATHSENSE_STAGE2_DRY;
  } else {
    result->decision = BREATHSENSE_STAGE2_WET;
  }

  return true;
}
