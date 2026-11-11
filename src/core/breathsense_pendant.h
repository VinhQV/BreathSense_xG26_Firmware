#ifndef BREATHSENSE_PENDANT_H
#define BREATHSENSE_PENDANT_H

#include <stdint.h>

#define BREATHSENSE_PENDANT_STATUS_SIZE 6u

void breathsense_pendant_init(void);
void breathsense_pendant_status(uint8_t data[BREATHSENSE_PENDANT_STATUS_SIZE]);

#endif
