#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "relay_core.h"

int main(void)
{
  relay_queue_t queue = {0};
  uint8_t packet[8] = {0};
  assert(relay_queue_peek(&queue) == NULL);
  assert(!relay_queue_push(&queue, packet, 7));
  assert(queue.received == 0);
  for (unsigned i = 0; i < RELAY_QUEUE_CAPACITY; ++i) {
    packet[0] = (uint8_t)i;
    assert(relay_queue_push(&queue, packet, sizeof(packet)));
  }
  const uint8_t *pending = relay_queue_peek(&queue);
  assert(pending[0] == 0);
  assert(!relay_queue_push(&queue, packet, sizeof(packet)));
  assert(queue.dropped == 1 && relay_queue_peek(&queue) == pending);
  /* Losing the gateway does not commit the pending head. Re-send is identical. */
  assert(relay_queue_peek(&queue)[0] == 0);
  for (unsigned i = 0; i < RELAY_QUEUE_CAPACITY; ++i) {
    assert(relay_queue_peek(&queue)[0] == i);
    relay_queue_commit(&queue);
  }
  assert(queue.count == 0 && queue.delivered == RELAY_QUEUE_CAPACITY);
  relay_queue_commit(&queue);
  assert(queue.delivered == RELAY_QUEUE_CAPACITY);
  for (unsigned i = 0; i < 10000; ++i) {
    relay_put_u32(packet, i);
    assert(relay_queue_push(&queue, packet, sizeof(packet)));
    assert(memcmp(relay_queue_peek(&queue), packet, 8) == 0);
    relay_queue_commit(&queue);
  }
  uint8_t ad[] = {2, 1, 6, 12, 9, 'M','y','D','e','v','i','c','e','_','0','1'};
  assert(relay_advertisement_has_name(ad, sizeof(ad), "MyDevice_01"));
  assert(!relay_advertisement_has_name(ad, sizeof(ad), "MyDevice"));
  assert(!relay_advertisement_has_name(ad, sizeof(ad), "BSRelay_01"));
  for (size_t length = 0; length < sizeof(ad); ++length) {
    assert(!relay_advertisement_has_name(ad, length, "MyDevice_01"));
  }
  ad[3] = 255;
  assert(!relay_advertisement_has_name(ad, sizeof(ad), "MyDevice_01"));
  uint8_t address[6] = {0};
  const uint8_t expected[] = {0xff,0xee,0xdd,0xcc,0xbb,0xaa};
  assert(relay_parse_address("Aa:BB:cc:DD:ee:Ff", address));
  assert(memcmp(address, expected, 6) == 0);
  assert(!relay_parse_address("AA:BB:CC:DD:EE:GG", address));
  assert(memcmp(address, expected, 6) == 0);
  assert(!relay_parse_address("AA-BB-CC-DD-EE-FF", address));
  puts("PASS: queue overflow, indication retry, FIFO wrap, malformed ads, identity parsing");
  return 0;
}
