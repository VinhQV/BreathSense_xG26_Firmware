/***************************************************************************//**
 * @file breathsense_time_sync.h
 * @brief Unix-time synchronization based on the monotonic sleeptimer clock.
 ******************************************************************************/

#ifndef BREATHSENSE_TIME_SYNC_H
#define BREATHSENSE_TIME_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void breathsense_time_sync_init(void);

/* Reject an unset Pi clock. Supported UTC dates: 2020-01-01 through 2099. */
#define BREATHSENSE_TIME_MIN_EPOCH 1577836800u
#define BREATHSENSE_TIME_MAX_EPOCH 4102444799u
bool breathsense_time_sync_set_epoch(uint32_t epoch_s);

bool breathsense_time_sync_get_epoch(uint32_t *epoch_s);

bool breathsense_time_sync_is_valid(void);

#ifdef __cplusplus
}
#endif

#endif /* BREATHSENSE_TIME_SYNC_H */
