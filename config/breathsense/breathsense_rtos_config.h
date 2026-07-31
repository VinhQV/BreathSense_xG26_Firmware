#ifndef BREATHSENSE_RTOS_CONFIG_H
#define BREATHSENSE_RTOS_CONFIG_H

/*
 * Cau hinh task cua firmware BreathSense.
 *
 * Micrium OS su dung gia tri priority nho hon
 * de bieu dien muc uu tien cao hon.
 *
 * Thu tu:
 * AI task > Sensor task > BLE TX task
 */

/*
 * AI task.
 */
#define BS_AI_TASK_PRIORITY               21u
#define BS_AI_TASK_STACK_SIZE             1024u

/*
 * Sensor task.
 */
#define BS_SENSOR_TASK_PRIORITY           25u
#define BS_SENSOR_TASK_STACK_SIZE         512u

/*
 * BLE TX task.
 */
#define BS_BLE_TX_TASK_PRIORITY           26u
#define BS_BLE_TX_TASK_STACK_SIZE         384u

/*
 * Cau hinh thoi gian cua Si7021.
 */
#define BS_SENSOR_STARTUP_DELAY_MS        50u
#define BS_SENSOR_SAMPLE_PERIOD_MS        5000u

#endif /* BREATHSENSE_RTOS_CONFIG_H */