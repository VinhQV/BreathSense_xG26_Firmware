/***************************************************************************//**
 * @file breathsense_indicator.c
 * @brief Battery-aware RGB LED state indicator for xG26-DK2608A.
 *
 * Product behavior:
 *   COUGH  -> solid red
 *   SPEECH -> solid blue
 *   SNEEZE -> solid green
 *   OTHER  -> off
 ******************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include "breathsense_indicator.h"

#include "sl_led.h"
#include "sl_simple_rgb_pwm_led_instances.h"

/* PWM level range is 0..65535. 8192 is about 12.5%. */
#define BS_RGB_LED_LEVEL  8192u

static bool indicator_initialized = false;
static breathsense_ai_class_t displayed_class = BREATHSENSE_AI_OTHER;

static void rgb_led_off(void)
{
  sl_led_turn_off((sl_led_t *)&sl_simple_rgb_pwm_led_rgb_led0);
}

static void rgb_led_set(uint16_t red,
                        uint16_t green,
                        uint16_t blue)
{
  rgb_led_off();

  sl_led_set_rgb_color(&sl_simple_rgb_pwm_led_rgb_led0,
                       red,
                       green,
                       blue);

  sl_led_turn_on((sl_led_t *)&sl_simple_rgb_pwm_led_rgb_led0);
}

void breathsense_indicator_init(void)
{
  displayed_class = BREATHSENSE_AI_OTHER;
  indicator_initialized = true;
  rgb_led_off();
}

void breathsense_indicator_set_class(
  breathsense_ai_class_t class_id)
{
  if (!indicator_initialized) {
    breathsense_indicator_init();
  }

  if (class_id == displayed_class) {
    return;
  }

  switch (class_id) {
    case BREATHSENSE_AI_COUGH:
      rgb_led_set(BS_RGB_LED_LEVEL, 0u, 0u);
      break;

    case BREATHSENSE_AI_SPEECH:
      rgb_led_set(0u, 0u, BS_RGB_LED_LEVEL);
      break;

    case BREATHSENSE_AI_SNEEZE:
      rgb_led_set(0u, BS_RGB_LED_LEVEL, 0u);
      break;

    case BREATHSENSE_AI_OTHER:
    default:
      class_id = BREATHSENSE_AI_OTHER;
      rgb_led_off();
      break;
  }

  displayed_class = class_id;
}
