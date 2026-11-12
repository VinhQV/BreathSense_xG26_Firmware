#ifndef BREATHSENSE_RELAY_CORE_H
#define BREATHSENSE_RELAY_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "relay_config.h"

typedef struct {
  uint8_t packets[RELAY_QUEUE_CAPACITY][8];
  uint16_t head;
  uint16_t count;
  uint32_t received;
  uint32_t delivered;
  uint32_t dropped;
} relay_queue_t;

bool relay_queue_push(relay_queue_t *queue, const uint8_t *data, size_t size);
const uint8_t *relay_queue_peek(const relay_queue_t *queue);
void relay_queue_commit(relay_queue_t *queue);
bool relay_advertisement_has_name(const uint8_t *data, size_t size,
                                 const char *name);
bool relay_parse_address(const char *text, uint8_t address_le[6]);
void relay_put_u32(uint8_t *dst, uint32_t value);
#endif
