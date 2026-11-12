#ifndef BREATHSENSE_AI_PRODUCT_CONFIG_H
#define BREATHSENSE_AI_PRODUCT_CONFIG_H

/*
 * Product logging:
 *   0 = only print confirmed label changes and errors
 *   1 = also print probabilities, timing and audio level every inference
 */
#define BS_AI_DEBUG_RAW_LOGS                 1u

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
#define BS_AI_SNEEZE_MIN_PROBABILITY         0.70f
#define BS_AI_SPEECH_MIN_PROBABILITY         0.55f

/*
 * Prioritize COUGH when SPEECH or OTHER wins by only a small margin.
 *
 * Example:
 *   COUGH = 45%
 *   SPEECH = 55%
 *   GAP = 10%
 *
 * Because COUGH >= 40% and GAP <= 15%, COUGH becomes the candidate.
 */
#define BS_AI_COUGH_PRIORITY_MIN_PROBABILITY     0.35f
#define BS_AI_COUGH_PRIORITY_MAX_GAP             0.20f
#define BS_AI_COUGH_PRIORITY_STRONG_PROBABILITY  0.65f

/*
 * A very strong result may be accepted after one window.
 * Otherwise the same candidate must be seen in two consecutive windows.
 */
#define BS_AI_COUGH_STRONG_PROBABILITY       0.85f
#define BS_AI_SNEEZE_STRONG_PROBABILITY      0.80f
#define BS_AI_SPEECH_STRONG_PROBABILITY      0.90f

#define BS_AI_REQUIRED_HITS_EVENT            2u
#define BS_AI_REQUIRED_HITS_OTHER            2u

/*
 * Stage 2 dry/wet decision band.
 *
 * The binary model output is converted from quantized logits to an estimated
 * wet probability. Values inside the open interval below are reported as
 * UNKNOWN instead of forcing a dry/wet decision.
 *
 * These are engineering thresholds, not clinical thresholds. Validate them
 * on a labelled dry/wet test set before making product claims.
 */
#define BS_AI_STAGE2_DRY_MAX_WET_PERCENT     30u
#define BS_AI_STAGE2_WET_MIN_WET_PERCENT     70u

/*
 * Cough-bout classification.
 *
 * A bout starts when the stable Stage 1 class becomes COUGH and ends when the
 * stable class leaves COUGH. A bout lasting at least this long is marked as
 * prolonged in the event metadata.
 *
 * This is an engineering threshold for product validation. It is not a
 * diagnostic or clinical threshold and must be validated with domain experts
 * before any medical claim is made.
 */
#define BS_AI_PROLONGED_COUGH_MIN_DURATION_MS  4000u

#endif /* BREATHSENSE_AI_PRODUCT_CONFIG_H */
