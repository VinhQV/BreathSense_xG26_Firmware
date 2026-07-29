/***************************************************************************//**
 * @file breathsense_mfcc_runtime.cc
 * @brief Reusable BreathSense CMSIS-DSP MFCC frontend.
 ******************************************************************************/

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "arm_math.h"

#include "breathsense_mfcc_coefficients.h"
#include "breathsense_mfcc_config.h"
#include "breathsense_mfcc_runtime.h"

namespace {

alignas(16) static float fft_input[BS_MFCC_FFT_SIZE];
alignas(16) static float fft_output[BS_MFCC_FFT_SIZE];
alignas(16) static float power_spectrum[BS_MFCC_SPECTRUM_BINS];
alignas(16) static float log_mel[BS_MFCC_MEL_COUNT];

static arm_rfft_fast_instance_f32 rfft_instance;
static bool initialized = false;

enum class PcmFormat {
  Float32,
  Int16
};

static int32_t round_to_nearest_even(float value)
{
  const float lower_float = std::floor(value);
  const int32_t lower = static_cast<int32_t>(lower_float);
  const float fraction = value - lower_float;

  if (fraction < 0.5f) {
    return lower;
  }

  if (fraction > 0.5f) {
    return lower + 1;
  }

  return ((lower & 1) == 0) ? lower : (lower + 1);
}

static int8_t quantize_model_input(float normalized_mfcc)
{
  int32_t quantized =
    round_to_nearest_even(normalized_mfcc / BS_MODEL_INPUT_SCALE)
    + BS_MODEL_INPUT_ZERO_POINT;

  if (quantized > 127) {
    quantized = 127;
  } else if (quantized < -128) {
    quantized = -128;
  }

  return static_cast<int8_t>(quantized);
}

static void calculate_power_spectrum(void)
{
  power_spectrum[0] = fft_output[0] * fft_output[0];

  for (std::size_t bin = 1; bin < (BS_MFCC_FFT_SIZE / 2u); ++bin) {
    const float real = fft_output[2u * bin];
    const float imaginary = fft_output[(2u * bin) + 1u];

    power_spectrum[bin] =
      (real * real) + (imaginary * imaginary);
  }

  power_spectrum[BS_MFCC_FFT_SIZE / 2u] =
    fft_output[1] * fft_output[1];
}

static void calculate_log_mel_energies(void)
{
  for (std::size_t mel = 0; mel < BS_MFCC_MEL_COUNT; ++mel) {
    const std::size_t spectrum_start = g_bs_mel_start[mel];
    const std::size_t weight_start = g_bs_mel_offset[mel];
    const std::size_t count = g_bs_mel_length[mel];

    float energy = 0.0f;

    for (std::size_t index = 0; index < count; ++index) {
      energy +=
        power_spectrum[spectrum_start + index]
        * g_bs_mel_weights[weight_start + index];
    }

    log_mel[mel] = std::log(energy + BS_MFCC_LOG_EPSILON);
  }
}

static float calculate_mfcc_coefficient(std::size_t coefficient)
{
  const float *dct_row =
    &g_bs_dct_basis[coefficient * BS_MFCC_MEL_COUNT];

  float result = 0.0f;

  for (std::size_t mel = 0; mel < BS_MFCC_MEL_COUNT; ++mel) {
    result += dct_row[mel] * log_mel[mel];
  }

  return result;
}

static float read_pcm_sample(const void *pcm,
                             PcmFormat format,
                             std::size_t index)
{
  if (format == PcmFormat::Int16) {
    const int16_t *pcm16 =
      static_cast<const int16_t *>(pcm);

    return static_cast<float>(pcm16[index]) / 32768.0f;
  }

  const float *pcm_f32 =
    static_cast<const float *>(pcm);

  return pcm_f32[index];
}

static bool process_internal(const void *pcm,
                             PcmFormat format,
                             std::size_t pcm_sample_count,
                             int8_t *output_int8,
                             std::size_t output_count)
{
  if (pcm == nullptr
      || output_int8 == nullptr
      || output_count < BS_MFCC_ELEMENT_COUNT
      || pcm_sample_count > BS_AUDIO_WINDOW_SAMPLES) {
    return false;
  }

  if (!breathsense_mfcc_runtime_init()) {
    return false;
  }

  for (std::size_t frame = 0; frame < BS_MFCC_FRAME_COUNT; ++frame) {
    const std::size_t frame_start = frame * BS_MFCC_HOP_LENGTH;

    for (std::size_t sample = 0; sample < BS_MFCC_FFT_SIZE; ++sample) {
      const std::size_t source_index = frame_start + sample;

      const float pcm_sample =
        (source_index < pcm_sample_count)
        ? read_pcm_sample(pcm, format, source_index)
        : 0.0f;

      fft_input[sample] =
        pcm_sample * g_bs_hann_window[sample];
    }

    arm_rfft_fast_f32(&rfft_instance,
                      fft_input,
                      fft_output,
                      0);

    calculate_power_spectrum();
    calculate_log_mel_energies();

    for (std::size_t coefficient = 0;
         coefficient < BS_MFCC_COEFFICIENT_COUNT;
         ++coefficient) {
      const float raw_mfcc =
        calculate_mfcc_coefficient(coefficient);

      const float normalized_mfcc =
        (raw_mfcc - BS_MFCC_GLOBAL_MEAN)
        / BS_MFCC_GLOBAL_STD;

      const std::size_t tensor_index =
        (coefficient * BS_MFCC_FRAME_COUNT) + frame;

      output_int8[tensor_index] =
        quantize_model_input(normalized_mfcc);
    }
  }

  return true;
}

}  // namespace

extern "C" bool breathsense_mfcc_runtime_init(void)
{
  if (initialized) {
    return true;
  }

  const arm_status status =
    arm_rfft_fast_init_1024_f32(&rfft_instance);

  initialized = (status == ARM_MATH_SUCCESS);
  return initialized;
}

extern "C" bool breathsense_mfcc_runtime_process_f32(
  const float *pcm,
  size_t pcm_sample_count,
  int8_t *output_int8,
  size_t output_count)
{
  return process_internal(pcm,
                          PcmFormat::Float32,
                          pcm_sample_count,
                          output_int8,
                          output_count);
}

extern "C" bool breathsense_mfcc_runtime_process_pcm16(
  const int16_t *pcm,
  size_t pcm_sample_count,
  int8_t *output_int8,
  size_t output_count)
{
  return process_internal(pcm,
                          PcmFormat::Int16,
                          pcm_sample_count,
                          output_int8,
                          output_count);
}
