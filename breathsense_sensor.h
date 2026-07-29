#ifndef BREATHSENSE_SENSOR_H
#define BREATHSENSE_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int32_t temperature_mc;
  uint32_t humidity_x1000;
  uint32_t sequence;
  bool valid;
} breathsense_sensor_result_t;

/*
 * Khoi tao Si7021 task, event flag group va software timer.
 */
void breathsense_sensor_init(void);

/*
 * Tao mot yeu cau do ngay lap tuc ma khong can polling.
 * Ham nay huu ich cho test hoac lenh noi bo sau nay.
 */
void breathsense_sensor_trigger_now(void);

/*
 * Sao chep mau sensor moi nhat.
 */
bool breathsense_sensor_get_latest(
  breathsense_sensor_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* BREATHSENSE_SENSOR_H */
