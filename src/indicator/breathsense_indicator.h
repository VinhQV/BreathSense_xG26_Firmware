/***************************************************************************//**
 * @file breathsense_indicator.h
 * @brief RGB LED indication for stable BreathSense AI classes.
 ******************************************************************************/

#ifndef BREATHSENSE_INDICATOR_H
#define BREATHSENSE_INDICATOR_H

#include "breathsense_ai.h"

#ifdef __cplusplus
extern "C" {
#endif

void breathsense_indicator_init(void);

void breathsense_indicator_set_class(
  breathsense_ai_class_t class_id
);

#ifdef __cplusplus
}
#endif

#endif  // BREATHSENSE_INDICATOR_H
