/***************************************************************************//**
 * @file breathsense_inference_runtime.cc
 * @brief Reusable TFLite Micro runtime for the BreathSense 4-class model.
 ******************************************************************************/

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "breathsense_inference_runtime.h"
#include "breathsense_model_data.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

constexpr std::size_t kTensorArenaSize = 96u * 1024u;
constexpr int kOperatorCount = 5;

alignas(16) static std::uint8_t tensor_arena[kTensorArenaSize];

static tflite::MicroInterpreter *interpreter = nullptr;
static TfLiteTensor *input_tensor = nullptr;
static TfLiteTensor *output_tensor = nullptr;
static bool initialized = false;

static bool add_required_operators(
  tflite::MicroMutableOpResolver<kOperatorCount>& resolver)
{
  if (resolver.AddConv2D() != kTfLiteOk) {
    std::printf("Inference init: AddConv2D failed\r\n");
    return false;
  }

  if (resolver.AddMaxPool2D() != kTfLiteOk) {
    std::printf("Inference init: AddMaxPool2D failed\r\n");
    return false;
  }

  if (resolver.AddReduceMax() != kTfLiteOk) {
    std::printf("Inference init: AddReduceMax failed\r\n");
    return false;
  }

  if (resolver.AddFullyConnected() != kTfLiteOk) {
    std::printf("Inference init: AddFullyConnected failed\r\n");
    return false;
  }

  if (resolver.AddSoftmax() != kTfLiteOk) {
    std::printf("Inference init: AddSoftmax failed\r\n");
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
         && tensor->bytes == BREATHSENSE_MODEL_INPUT_COUNT;
}

static bool output_tensor_is_valid(const TfLiteTensor *tensor)
{
  return tensor != nullptr
         && tensor->type == kTfLiteInt8
         && tensor->dims != nullptr
         && tensor->dims->size == 2
         && tensor->dims->data[0] == 1
         && tensor->dims->data[1] == 4
         && tensor->bytes == BREATHSENSE_MODEL_OUTPUT_COUNT;
}

}  // namespace

extern "C" bool breathsense_inference_runtime_init(void)
{
  if (initialized) {
    return true;
  }

  const tflite::Model *model =
    tflite::GetModel(g_breathsense_model_data);

  if (model == nullptr) {
    std::printf("Inference init FAIL: model pointer is null\r\n");
    return false;
  }

  if (model->version() != TFLITE_SCHEMA_VERSION) {
    std::printf("Inference init FAIL: schema model=%ld runtime=%d\r\n",
                static_cast<long>(model->version()),
                TFLITE_SCHEMA_VERSION);
    return false;
  }

  static tflite::MicroMutableOpResolver<kOperatorCount> resolver;
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
    tensor_arena,
    kTensorArenaSize
  );

  interpreter = &static_interpreter;

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    std::printf("Inference init FAIL: AllocateTensors, arena=%lu bytes\r\n",
                static_cast<unsigned long>(kTensorArenaSize));
    interpreter = nullptr;
    return false;
  }

  input_tensor = interpreter->input(0);
  output_tensor = interpreter->output(0);

  if (!input_tensor_is_valid(input_tensor)) {
    std::printf("Inference init FAIL: expected INT8 input [1,40,50,1]\r\n");
    interpreter = nullptr;
    input_tensor = nullptr;
    output_tensor = nullptr;
    return false;
  }

  if (!output_tensor_is_valid(output_tensor)) {
    std::printf("Inference init FAIL: expected INT8 output [1,4]\r\n");
    interpreter = nullptr;
    input_tensor = nullptr;
    output_tensor = nullptr;
    return false;
  }

  initialized = true;

  std::printf("BreathSense inference runtime ready: "
              "input_scale=%.9f input_zp=%ld "
              "output_scale=%.9f output_zp=%ld\r\n",
              static_cast<double>(input_tensor->params.scale),
              static_cast<long>(input_tensor->params.zero_point),
              static_cast<double>(output_tensor->params.scale),
              static_cast<long>(output_tensor->params.zero_point));

  return true;
}

extern "C" bool breathsense_inference_runtime_run(
  const int8_t *input_int8,
  size_t input_count,
  int8_t output_int8[BREATHSENSE_MODEL_OUTPUT_COUNT],
  float probabilities[BREATHSENSE_MODEL_OUTPUT_COUNT])
{
  if (input_int8 == nullptr
      || output_int8 == nullptr
      || input_count != BREATHSENSE_MODEL_INPUT_COUNT) {
    return false;
  }

  if (!breathsense_inference_runtime_init()) {
    return false;
  }

  std::memcpy(input_tensor->data.int8,
              input_int8,
              BREATHSENSE_MODEL_INPUT_COUNT);

  if (interpreter->Invoke() != kTfLiteOk) {
    std::printf("BreathSense inference FAIL: Invoke()\r\n");
    return false;
  }

  for (std::size_t i = 0;
       i < BREATHSENSE_MODEL_OUTPUT_COUNT;
       ++i) {
    const int8_t quantized = output_tensor->data.int8[i];

    output_int8[i] = quantized;

    if (probabilities != nullptr) {
      probabilities[i] =
        (static_cast<int32_t>(quantized)
         - output_tensor->params.zero_point)
        * output_tensor->params.scale;
    }
  }

  return true;
}
