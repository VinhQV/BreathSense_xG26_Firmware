#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Khoi tao cac module cua ung dung BreathSense.
 */
void app_init(void);

/*
 * Khong xu ly polling tai day.
 * Ham duoc giu de tuong thich voi Silicon Labs application framework.
 */
void app_process_action(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
