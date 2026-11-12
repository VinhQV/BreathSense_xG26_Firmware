#include "relay_core.h"
#include <string.h>

bool relay_queue_push(relay_queue_t *queue, const uint8_t *data, size_t size)
{
  if (data == NULL || size != 8u) { return false; }
  ++queue->received;
  if (queue->count == RELAY_QUEUE_CAPACITY) {
    /* Keep the head stable while an indication is awaiting confirmation. */
    ++queue->dropped;
    return false;
  }
  uint16_t tail = (queue->head + queue->count) % RELAY_QUEUE_CAPACITY;
  memcpy(queue->packets[tail], data, 8u);
  ++queue->count;
  return true;
}

const uint8_t *relay_queue_peek(const relay_queue_t *queue)
{
  return queue->count != 0u ? queue->packets[queue->head] : NULL;
}

void relay_queue_commit(relay_queue_t *queue)
{
  if (queue->count == 0u) { return; }
  queue->head = (queue->head + 1u) % RELAY_QUEUE_CAPACITY;
  --queue->count;
  ++queue->delivered;
}

bool relay_advertisement_has_name(const uint8_t *data, size_t size,
                                 const char *name)
{
  size_t name_size = strlen(name);
  for (size_t offset = 0u; offset < size;) {
    size_t field_size = data[offset];
    if (field_size == 0u) { break; }
    if (field_size > size - offset - 1u) { return false; }
    /* Require a complete exact name: never join another relay or a prefix. */
    if (data[offset + 1u] == 0x09u && field_size == name_size + 1u
        && memcmp(data + offset + 2u, name, name_size) == 0) { return true; }
    offset += field_size + 1u;
  }
  return false;
}

static int hex_digit(char ch)
{
  if (ch >= '0' && ch <= '9') { return ch - '0'; }
  if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
  if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
  return -1;
}

bool relay_parse_address(const char *text, uint8_t address_le[6])
{
  uint8_t parsed[6];
  if (strlen(text) != 17u) { return false; }
  for (unsigned i = 0u; i < 6u; ++i) {
    int hi = hex_digit(text[i * 3u]), lo = hex_digit(text[i * 3u + 1u]);
    if (hi < 0 || lo < 0 || (i != 5u && text[i * 3u + 2u] != ':')) {
      return false;
    }
    parsed[5u - i] = (uint8_t)((hi << 4) | lo);
  }
  memcpy(address_le, parsed, sizeof(parsed));
  return true;
}

void relay_put_u32(uint8_t *dst, uint32_t value)
{
  for (unsigned i = 0u; i < 4u; ++i) { dst[i] = (uint8_t)(value >> (8u * i)); }
}
