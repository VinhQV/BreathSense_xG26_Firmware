#ifndef BREATHSENSE_AI_PRODUCT_CONFIG_H
#define BREATHSENSE_AI_PRODUCT_CONFIG_H

/*
 * Product logging:
 *   0 = only print confirmed label changes and errors
 *   1 = also print probabilities, timing and audio level every inference
 */
#define BS_AI_DEBUG_RAW_LOGS                 0u

/*
 * Audio gate. Windows below both values are treated as OTHER.
 * Recalibrate after changing microphone gain, board or enclosure.
 */
#define BS_AI_AUDIO_GATE_PEAK                250u
#define BS_AI_AUDIO_GATE_MEAN_ABS            20u

/*
 * Minimum probability for a class to become a candidate.
 */
#define BS_AI_COUGH_MIN_PROBABILITY          0.70f
#define BS_AI_OTHER_MIN_PROBABILITY          0.50f
#define BS_AI_SNEEZE_MIN_PROBABILITY         0.75f
#define BS_AI_SPEECH_MIN_PROBABILITY         0.55f

/*
 * A very strong result may be accepted after one window.
 * Otherwise the same candidate must be seen in two consecutive windows.
 */
#define BS_AI_COUGH_STRONG_PROBABILITY       0.95f
#define BS_AI_SNEEZE_STRONG_PROBABILITY      0.95f
#define BS_AI_SPEECH_STRONG_PROBABILITY      0.90f

#define BS_AI_REQUIRED_HITS_EVENT            2u
#define BS_AI_REQUIRED_HITS_OTHER            2u

#endif
