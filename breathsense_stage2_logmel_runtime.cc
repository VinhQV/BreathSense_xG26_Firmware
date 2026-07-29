/***************************************************************************//**
 * @file breathsense_stage2_logmel_runtime.cc
 * @brief CMSIS-DSP Log-Mel frontend for BreathSense Stage 2.
 ******************************************************************************/

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "arm_math.h"

#include "breathsense_stage2_logmel_coefficients.h"
#include "breathsense_stage2_logmel_runtime.h"

namespace {

constexpr std::size_t kFftSize = 1024u;
constexpr std::size_t kHalfFftSize = 512u;
constexpr std::size_t kSpectrumBins = 513u;
constexpr std::size_t kHopLength = 512u;
constexpr std::size_t kMelCount = 40u;
constexpr std::size_t kFrameCount = 50u;
constexpr std::size_t kElementCount = 2000u;

constexpr float kPcmScale = 1.0f / 32768.0f;
constexpr float kPowerToDbFactor = 10.0f;
constexpr float kAmin = 1.0e-10f;
constexpr float kTopDb = 80.0f;
constexpr float kNormalizationEpsilon = 1.0e-8f;

constexpr float kInputScale = 0.003921568859368563f;
constexpr int32_t kInputZeroPoint = -128;

alignas(16) static float fft_input[kFftSize];
alignas(16) static float fft_output[kFftSize];
alignas(16) static float power_spectrum[kSpectrumBins];

/*
 * First stores Mel power, then stores dB values in-place.
 * Layout is mel-major:
 *   index = mel * 50 + frame
 */
alignas(16) static float logmel_workspace[kElementCount];

static arm_rfft_fast_instance_f32 rfft_instance;
static bool initialized = false;

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

static int8_t quantize_input(float normalized)
{
  int32_t quantized =
    round_to_nearest_even(normalized / kInputScale)
    + kInputZeroPoint;

  if (quantized > 127) {
    quantized = 127;
  } else if (quantized < -128) {
    quantized = -128;
  }

  return static_cast<int8_t>(quantized);
}

/*
 * The contract first crops/pads audio to 25,600 samples, then applies
 * reflection padding of 512 samples for centered framing.
 *
 * Only frames 0..49 are retained. Their largest source index is 25,599,
 * so only left reflection is required for the retained frames.
 */
static float read_centered_pcm16(const int16_t *pcm,
                                 std::size_t pcm_sample_count,
                                 int32_t source_index)
{
  if (source_index < 0) {
    source_index = -source_index;
  }

  if (source_index
      >= static_cast<int32_t>(
           BREATHSENSE_STAGE2_AUDIO_WINDOW_SAMPLES)) {
    return 0.0f;
  }

  if (source_index
      >= static_cast<int32_t>(pcm_sample_count)) {
    return 0.0f;
  }

  return static_cast<float>(pcm[source_index]) * kPcmScale;
}

static void calculate_power_spectrum(void)
{
  power_spectrum[0] = fft_output[0] * fft_output[0];

  for (std::size_t bin = 1u; bin < kHalfFftSize; ++bin) {
    const float real = fft_output[2u * bin];
    const float imaginary = fft_output[(2u * bin) + 1u];

    power_spectrum[bin] =
      (real * real) + (imaginary * imaginary);
  }

  power_spectrum[kHalfFftSize] =
    fft_output[1] * fft_output[1];
}

static float calculate_mel_power(std::size_t mel)
{
  const std::size_t spectrum_start =
    g_breathsense_stage2_mel_start[mel];

  const std::size_t weight_start =
    g_breathsense_stage2_mel_offset[mel];

  const std::size_t count =
    g_breathsense_stage2_mel_length[mel];

  float energy = 0.0f;

  for (std::size_t index = 0u; index < count; ++index) {
    energy +=
      power_spectrum[spectrum_start + index]
      * g_breathsense_stage2_mel_weights[
          weight_start + index
        ];
  }

  return energy;
}

}  // namespace

extern "C" bool breathsense_stage2_logmel_runtime_init(void)
{
  if (initialized) {
    return true;
  }

  const arm_status status =
    arm_rfft_fast_init_1024_f32(&rfft_instance);

  initialized = (status == ARM_MATH_SUCCESS);
  return initialized;
}

extern "C" bool breathsense_stage2_logmel_runtime_process_pcm16(
  const int16_t *pcm,
  size_t pcm_sample_count,
  int8_t *output_int8,
  size_t output_count)
{
  if (pcm == nullptr
      || output_int8 == nullptr
      || output_count < kElementCount
      || pcm_sample_count
           > BREATHSENSE_STAGE2_AUDIO_WINDOW_SAMPLES) {
    return false;
  }

  if (!breathsense_stage2_logmel_runtime_init()) {
    return false;
  }

  float maximum_mel_power = 0.0f;

  /*
   * Librosa produces 51 frames with center=true. The contract discards
   * frame index 50, so only 0..49 are generated here.
   */
  for (std::size_t frame = 0u;
       frame < kFrameCount;
       ++frame) {
    const int32_t frame_source_start =
      static_cast<int32_t>(frame * kHopLength)
      - static_cast<int32_t>(kHalfFftSize);

    for (std::size_t sample = 0u;
         sample < kFftSize;
         ++sample) {
      const int32_t source_index =
        frame_source_start + static_cast<int32_t>(sample);

      fft_input[sample] =
        read_centered_pcm16(pcm,
                            pcm_sample_count,
                            source_index)
        * g_breathsense_stage2_hann[sample];
    }

    arm_rfft_fast_f32(&rfft_instance,
                      fft_input,
                      fft_output,
                      0);

    calculate_power_spectrum();

    for (std::size_t mel = 0u; mel < kMelCount; ++mel) {
      const float mel_power = calculate_mel_power(mel);
      const std::size_t tensor_index =
        (mel * kFrameCount) + frame;

      logmel_workspace[tensor_index] = mel_power;

      if (mel_power > maximum_mel_power) {
        maximum_mel_power = mel_power;
      }
    }
  }

  const float reference_power =
    (maximum_mel_power > kAmin)
    ? maximum_mel_power
    : kAmin;

  const float reference_db =
    kPowerToDbFactor * std::log10(reference_power);

  float minimum_db = std::numeric_limits<float>::max();
  float maximum_db = -std::numeric_limits<float>::max();

  for (std::size_t index = 0u;
       index < kElementCount;
       ++index) {
    const float safe_power =
      (logmel_workspace[index] > kAmin)
      ? logmel_workspace[index]
      : kAmin;

    float db =
      (kPowerToDbFactor * std::log10(safe_power))
      - reference_db;

    if (db < -kTopDb) {
      db = -kTopDb;
    }

    logmel_workspace[index] = db;

    if (db < minimum_db) {
      minimum_db = db;
    }

    if (db > maximum_db) {
      maximum_db = db;
    }
  }

  const float normalization_denominator =
    (maximum_db - minimum_db) + kNormalizationEpsilon;

  for (std::size_t index = 0u;
       index < kElementCount;
       ++index) {
    const float normalized =
      (logmel_workspace[index] - minimum_db)
      / normalization_denominator;

    output_int8[index] = quantize_input(normalized);
  }

  return true;
}
