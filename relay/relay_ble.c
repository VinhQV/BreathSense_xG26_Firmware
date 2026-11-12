/* BreathSense one-hop GATT bridge. All state is owned by the BT event loop. */
#include "relay_ble.h"
#include "relay_core.h"
#include "gatt_db.h"
#include "app_assert.h"
#include "app_log.h"
#include "sl_sleeptimer.h"
#include "sl_simple_led_instances.h"
#include <string.h>

#define INVALID_CONNECTION 0xffu
#define TICK_SIGNAL 1u
#define UUID_BYTES(id) {0x66,0x55,0x44,0x33,0x22,0x11,0x10,0x9e, \
                       0x6d,0x4c,0x4b,0x7a,(id),0x00,0xe0,0xb5}
static const uint8_t service_uuid[16] = UUID_BYTES(1);
static const uint8_t cough_uuid[16] = UUID_BYTES(2);
static const uint8_t environment_uuid[16] = UUID_BYTES(3);
static const uint8_t pendant_uuid[16] = UUID_BYTES(5);
static const uint8_t ai_uuid[16] = UUID_BYTES(6);

typedef enum {
  DOWN, CONNECTING, WAIT_PARAMETERS, DISCOVER_SERVICE, DISCOVER_CHARACTERISTICS,
  SUBSCRIBE_COUGH, SUBSCRIBE_ENVIRONMENT, RUNNING, READ_PENDANT, READ_AI, CLOSING
} upstream_phase_t;
typedef struct {
  uint8_t data[10];
  uint8_t size;
  bool valid;
  uint32_t updated_ms;
} cache_t;

static uint8_t upstream = INVALID_CONNECTION, gateway = INVALID_CONNECTION;
static uint8_t advertiser = 0xffu;
static upstream_phase_t phase = DOWN;
static bool booted, scanning, advertising, upstream_ready, source_bound;
static bool address_filter, indication_pending, environment_pending;
static bd_addr source_address, candidate_address;
static uint8_t source_type, candidate_type;
static uint16_t cough_cccd, environment_cccd, status_cccd;
static uint32_t service_handle, deadline_ms, retry_ms, next_poll_ms;
static uint16_t cough_handle, environment_handle, pendant_handle, ai_handle;
static uint8_t poll_index;
static int8_t upstream_rssi = 127, gateway_rssi = 127;
static uint16_t invalid_packets;
static cache_t last_cough, environment, pendant, ai;
static relay_queue_t queue;
static sl_sleeptimer_timer_handle_t tick_timer;

static uint32_t now_ms(void)
{
  uint64_t ms = 0;
  (void)sl_sleeptimer_tick64_to_ms(sl_sleeptimer_get_tick_count64(), &ms);
  return (uint32_t)ms;
}
static bool due(uint32_t now, uint32_t deadline)
{ return (int32_t)(now - deadline) >= 0; }
static bool fresh(const cache_t *cache)
{ return upstream_ready && cache->valid
         && (uint32_t)(now_ms() - cache->updated_ms) <= RELAY_CACHE_MAX_AGE_MS; }
static bool cache_update(cache_t *cache, const uint8_t *data, size_t size,
                         size_t expected)
{
  if (size != expected || size > sizeof(cache->data)) {
    ++invalid_packets;
    return false;
  }
  memcpy(cache->data, data, size);
  cache->size = (uint8_t)size;
  cache->updated_ms = now_ms();
  cache->valid = true;
  return true;
}
static void print_address(bd_addr address)
{
  app_log("%02X:%02X:%02X:%02X:%02X:%02X", address.addr[5], address.addr[4],
          address.addr[3], address.addr[2], address.addr[1], address.addr[0]);
}
static void set_phase(upstream_phase_t next)
{
  phase = next;
  deadline_ms = now_ms() + RELAY_PROCEDURE_TIMEOUT_MS;
}
static void leds(void)
{
  if (upstream_ready) { sl_led_turn_on(&sl_led_led0); }
  else { sl_led_turn_off(&sl_led_led0); }
  if (gateway != INVALID_CONNECTION && cough_cccd != 0u) { sl_led_turn_on(&sl_led_led1); }
  else { sl_led_turn_off(&sl_led_led1); }
}
static void status_bytes(uint8_t data[20])
{
  memset(data, 0, 20);
  data[0] = 1;
  data[1] = upstream_ready ? 3u : phase == DOWN ? 0u : phase == CONNECTING ? 1u : 2u;
  data[2] = (upstream != INVALID_CONNECTION && phase != CONNECTING ? 1u : 0u)
          | (upstream_ready ? 2u : 0u) | (gateway != INVALID_CONNECTION ? 4u : 0u)
          | ((cough_cccd & sl_bt_gatt_indication) != 0u ? 8u : 0u)
          | (fresh(&pendant) ? 16u : 0u) | (fresh(&ai) ? 32u : 0u);
  data[3] = (uint8_t)queue.count;
  data[4] = (uint8_t)upstream_rssi;
  data[5] = (uint8_t)gateway_rssi;
  relay_put_u32(data + 6, queue.received);
  relay_put_u32(data + 10, queue.delivered);
  relay_put_u32(data + 14, queue.dropped);
  data[18] = (uint8_t)invalid_packets;
  data[19] = (uint8_t)(invalid_packets >> 8);
}
static void notify_status(void)
{
  leds();
  if (gateway != INVALID_CONNECTION && (status_cccd & sl_bt_gatt_notification)) {
    uint8_t data[20];
    status_bytes(data);
    (void)sl_bt_gatt_server_send_notification(gateway, gattdb_relay_status,
                                             sizeof(data), data);
  }
}
static void start_advertising(void)
{
  /* Advertise diagnostics before finding a source; Pi must read source identity. */
  if (gateway != INVALID_CONNECTION || advertising) { return; }
  sl_status_t result = sl_bt_legacy_advertiser_start(advertiser,
                                                    sl_bt_legacy_advertiser_connectable);
  if (result == SL_STATUS_OK) { advertising = true; }
  else { app_log("RELAY: advertise retry error=0x%04lx\r\n", (unsigned long)result); }
}
static void reset_upstream(void)
{
  upstream = INVALID_CONNECTION;
  phase = DOWN;
  upstream_ready = false;
  service_handle = 0;
  cough_handle = environment_handle = pendant_handle = ai_handle = 0;
  pendant.valid = ai.valid = environment.valid = false;
  environment_pending = false;
  upstream_rssi = 127;
  retry_ms = now_ms() + RELAY_RETRY_MS;
  notify_status();
}
static void close_upstream(const char *reason, sl_status_t status)
{
  app_log("RELAY: source retry (%s) error=0x%04lx\r\n", reason, (unsigned long)status);
  upstream_ready = false;
  notify_status();
  set_phase(CLOSING);
  sl_status_t result = sl_bt_connection_close(upstream);
  if (result == SL_STATUS_INVALID_HANDLE) { reset_upstream(); }
}
static bool procedure(sl_status_t result, const char *name)
{
  if (result == SL_STATUS_OK) { return true; }
  close_upstream(name, result);
  return false;
}
static void start_scan(void)
{
  if (phase != DOWN || scanning || !due(now_ms(), retry_ms)) { return; }
  sl_status_t result = sl_bt_scanner_start(sl_bt_gap_phy_1m | sl_bt_gap_phy_coded,
                                          sl_bt_scanner_discover_observation);
  if (result == SL_STATUS_OK) {
    scanning = true;
    app_log("RELAY: scanning 1M+Coded for %s\r\n", RELAY_SOURCE_NAME);
  } else { retry_ms = now_ms() + RELAY_RETRY_MS; }
}
static void flush(void)
{
  if (gateway == INVALID_CONNECTION) { return; }
  const uint8_t *head = relay_queue_peek(&queue);
  if (head != NULL && cough_cccd != 0u && !indication_pending) {
    sl_status_t result;
    if ((cough_cccd & sl_bt_gatt_indication) != 0u) {
      result = sl_bt_gatt_server_send_indication(gateway, gattdb_cough_event, 8, head);
      if (result == SL_STATUS_OK) { indication_pending = true; }
    } else {
      result = sl_bt_gatt_server_send_notification(gateway, gattdb_cough_event, 8, head);
      /* Legacy mode confirms controller enqueue only, not Pi application receipt. */
      if (result == SL_STATUS_OK) { relay_queue_commit(&queue); }
    }
    /* Keep head on allocation/resource failures; periodic tick retries. */
  }
  if (environment_pending && fresh(&environment)
      && (environment_cccd & sl_bt_gatt_notification) != 0u) {
    if (sl_bt_gatt_server_send_notification(gateway, gattdb_environment_data,
        environment.size, environment.data) == SL_STATUS_OK) { environment_pending = false; }
  }
}
static void poll_status(void)
{
  if (upstream != INVALID_CONNECTION && phase != CONNECTING && phase != CLOSING) {
    (void)sl_bt_connection_get_median_rssi(upstream, &upstream_rssi);
  }
  if (gateway != INVALID_CONNECTION) {
    (void)sl_bt_connection_get_median_rssi(gateway, &gateway_rssi);
  }
  notify_status();
  app_log("RELAY: node=%u pi=%u queued=%u received=%lu delivered=%lu dropped=%lu rssi=%d/%d\r\n",
          (unsigned)upstream_ready, gateway != INVALID_CONNECTION, queue.count,
          (unsigned long)queue.received, (unsigned long)queue.delivered,
          (unsigned long)queue.dropped, upstream_rssi, gateway_rssi);
  if (phase != RUNNING) { return; }
  uint16_t handle = (poll_index++ & 1u) == 0u ? pendant_handle : ai_handle;
  if (handle == 0u) { return; }
  set_phase(handle == pendant_handle ? READ_PENDANT : READ_AI);
  (void)procedure(sl_bt_gatt_read_characteristic_value(upstream, handle), "read status");
}
static void tick_callback(sl_sleeptimer_timer_handle_t *handle, void *context)
{
  (void)handle; (void)context;
  (void)sl_bt_external_signal(TICK_SIGNAL);
}
static void scan_report(bd_addr address, uint8_t address_type, uint8_t phy,
                        uint8_t event_flags, const uint8_t *data, size_t size)
{
  if (phase != DOWN || !scanning || !(event_flags & 1u)) { return; }
  bool matches = (source_bound || address_filter)
      ? memcmp(address.addr, source_address.addr, 6) == 0
      : relay_advertisement_has_name(data, size, RELAY_SOURCE_NAME);
  if (!matches) { return; }
  if (source_bound && address_type != source_type) { return; }
  if (sl_bt_scanner_stop() != SL_STATUS_OK) { return; }
  scanning = false;
  candidate_address = address;
  candidate_type = address_type;
  set_phase(CONNECTING);
  sl_status_t result = sl_bt_connection_open(address, address_type,
                                            phy, &upstream);
  if (result != SL_STATUS_OK) { reset_upstream(); return; }
  app_log("RELAY: connecting source "); print_address(candidate_address); app_log("\r\n");
}
static void discovery_finished(uint16_t result)
{
  if (result != 0u) {
    /* Optional status reads must not take the event relay down. */
    if (phase == READ_PENDANT || phase == READ_AI) {
      if (phase == READ_PENDANT) { pendant.valid = false; }
      else { ai.valid = false; }
      phase = RUNNING;
      return;
    }
    close_upstream("GATT procedure", result); return;
  }
  switch (phase) {
    case DISCOVER_SERVICE:
      if (service_handle == 0u) { close_upstream("service missing", 0); break; }
      set_phase(DISCOVER_CHARACTERISTICS);
      (void)procedure(sl_bt_gatt_discover_characteristics(upstream, service_handle), "characteristics");
      break;
    case DISCOVER_CHARACTERISTICS:
      if (cough_handle == 0u || environment_handle == 0u) {
        close_upstream("required notify characteristics missing", 0); break;
      }
      if (!source_bound) {
        source_address = candidate_address;
        source_type = candidate_type;
        source_bound = true;
        app_log("RELAY: source pinned "); print_address(source_address); app_log("\r\n");
      }
      set_phase(SUBSCRIBE_COUGH);
      (void)procedure(sl_bt_gatt_set_characteristic_notification(upstream, cough_handle,
                            sl_bt_gatt_notification), "subscribe cough");
      break;
    case SUBSCRIBE_COUGH:
      set_phase(SUBSCRIBE_ENVIRONMENT);
      (void)procedure(sl_bt_gatt_set_characteristic_notification(upstream, environment_handle,
                            sl_bt_gatt_notification), "subscribe environment");
      break;
    case SUBSCRIBE_ENVIRONMENT:
      upstream_ready = true;
      phase = RUNNING;
      next_poll_ms = now_ms();
      start_advertising();
      notify_status();
      app_log("RELAY: source ready; advertising %s for Pi\r\n", RELAY_NAME);
      break;
    case READ_PENDANT: case READ_AI: phase = RUNNING; break;
    default: break;
  }
}
static void source_value(const sl_bt_evt_gatt_characteristic_value_t *value)
{
  if (value->connection != upstream) { return; }
  bool live_value = value->att_opcode == sl_bt_gatt_handle_value_notification
                 || value->att_opcode == sl_bt_gatt_handle_value_indication;
  if (value->characteristic == cough_handle && live_value) {
    if (cache_update(&last_cough, value->value.data, value->value.len, 8)) {
      bool queued = relay_queue_push(&queue, value->value.data, value->value.len);
      app_log("RELAY: cough counter=%u queued=%u\r\n",
              (unsigned)(value->value.data[6] | ((uint16_t)value->value.data[7] << 8)), queued);
    }
  } else if (value->characteristic == environment_handle && live_value) {
    if (cache_update(&environment, value->value.data, value->value.len, 4)) {
      environment_pending = true;
    }
  } else if (value->characteristic == pendant_handle && value->att_opcode == sl_bt_gatt_read_response) {
    (void)cache_update(&pendant, value->value.data, value->value.len, 6);
  } else if (value->characteristic == ai_handle && value->att_opcode == sl_bt_gatt_read_response) {
    (void)cache_update(&ai, value->value.data, value->value.len, 10);
  }
  if (value->att_opcode == sl_bt_gatt_handle_value_indication) {
    (void)sl_bt_gatt_send_characteristic_confirmation(upstream);
  }
  flush();
}
static void client_read(const sl_bt_evt_gatt_server_user_read_request_t *request)
{
  uint8_t status[20], identity[8];
  const uint8_t *data = NULL;
  size_t size = 0;
  uint8_t error = 0;
  const cache_t *cache = NULL;
  switch (request->characteristic) {
    case gattdb_relay_status: status_bytes(status); data = status; size = sizeof(status); break;
    case gattdb_relay_source:
      if (!source_bound) { error = 0x0eu; break; }
      identity[0] = 1; identity[1] = source_type;
      memcpy(identity + 2, source_address.addr, 6);
      data = identity; size = sizeof(identity); break;
    case gattdb_cough_event: cache = &last_cough; break;
    case gattdb_environment_data: cache = &environment; break;
    case gattdb_pendant_status: cache = &pendant; break;
    case gattdb_ai_status: cache = &ai; break;
    default: error = 0x0au; break;
  }
  if (cache != NULL) {
    if (!fresh(cache)) { error = 0x0eu; }
    else { data = cache->data; size = cache->size; }
  }
  if (error == 0u && request->offset > size) { error = 0x07u; }
  if (error == 0u) { data += request->offset; size -= request->offset; }
  else { data = NULL; size = 0; }
  (void)sl_bt_gatt_server_send_user_read_response(request->connection,
          request->characteristic, error, size, data, NULL);
}

void relay_on_event(sl_bt_msg_t *evt)
{
  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id: {
      app_log("\r\nBreathSense BLE relay v2 Long Range / BRD2709A / source=%s\r\n", RELAY_SOURCE_NAME);
      if (strlen(RELAY_SOURCE_ADDRESS) != 0u) {
        address_filter = relay_parse_address(RELAY_SOURCE_ADDRESS, source_address.addr);
        app_assert(address_filter, "Invalid RELAY_SOURCE_ADDRESS");
      }
      int16_t minimum_tx = 0, maximum_tx = 0;
      app_assert_status(sl_bt_system_set_tx_power(-100, 100, &minimum_tx, &maximum_tx));
      app_log("RELAY: TX limit=%d/10 dBm\r\n", maximum_tx);
      app_assert_status(sl_bt_advertiser_create_set(&advertiser));
      app_assert_status(sl_bt_gatt_server_write_attribute_value(gattdb_device_name,
                           0, strlen(RELAY_NAME), (const uint8_t *)RELAY_NAME));
      app_assert_status(sl_bt_legacy_advertiser_generate_data(advertiser,
                           sl_bt_advertiser_general_discoverable));
      app_assert_status(sl_bt_advertiser_set_timing(advertiser, 320, 320, 0, 0));
      app_assert_status(sl_bt_scanner_set_parameters(sl_bt_scanner_scan_mode_active, 160, 80));
      app_assert_status(sl_bt_connection_set_default_parameters(40, 80, 0, 600, 0, 0xffff));
      app_assert_status(sl_sleeptimer_start_periodic_timer_ms(&tick_timer,
                           RELAY_TICK_MS, tick_callback, NULL, 0, 0));
      booted = true;
      start_advertising();
      start_scan();
      break;
    }
    case sl_bt_evt_system_external_signal_id:
      if (!booted || !(evt->data.evt_system_external_signal.extsignals & TICK_SIGNAL)) { break; }
      if (phase != DOWN && phase != RUNNING && due(now_ms(), deadline_ms)) {
        close_upstream("timeout", SL_STATUS_TIMEOUT);
      }
      start_scan(); start_advertising(); flush();
      if (due(now_ms(), next_poll_ms)) {
        next_poll_ms = now_ms() + RELAY_POLL_MS;
        poll_status();
      }
      break;
    case sl_bt_evt_scanner_legacy_advertisement_report_id:
    {
      const sl_bt_evt_scanner_legacy_advertisement_report_t *r = &evt->data.evt_scanner_legacy_advertisement_report;
      scan_report(r->address, r->address_type, sl_bt_gap_phy_1m, r->event_flags, r->data.data, r->data.len);
      break;
    }
    case sl_bt_evt_scanner_extended_advertisement_report_id: {
      const sl_bt_evt_scanner_extended_advertisement_report_t *r = &evt->data.evt_scanner_extended_advertisement_report;
      scan_report(r->address, r->address_type, r->primary_phy, r->event_flags, r->data.data, r->data.len);
      break;
    }
    case sl_bt_evt_connection_phy_status_id:
      app_log("RELAY: %s active PHY=%u (4=Coded-S8, 8=Coded-S2, 1=1M)\r\n",
              evt->data.evt_connection_phy_status.connection == upstream ? "source" : "Pi",
              evt->data.evt_connection_phy_status.phy);
      break;
    case sl_bt_evt_connection_opened_id: {
      const sl_bt_evt_connection_opened_t *opened = &evt->data.evt_connection_opened;
      if (opened->role == sl_bt_connection_role_central) {
        if (opened->connection != upstream || phase != CONNECTING) {
          (void)sl_bt_connection_close(opened->connection); break;
        }
        set_phase(WAIT_PARAMETERS);
        app_log("RELAY: source connected; requesting Coded-S8\r\n");
        sl_status_t phy_result = sl_bt_connection_set_preferred_phy(upstream, 4u, 5u);
        if (phy_result != SL_STATUS_OK) { app_log("RELAY: PHY request failed 0x%lx\r\n", (unsigned long)phy_result); }
      } else if (gateway == INVALID_CONNECTION) {
        gateway = opened->connection;
        advertising = false;
        cough_cccd = environment_cccd = status_cccd = 0;
        indication_pending = false;
        app_log("RELAY: Pi connected\r\n");
        notify_status();
      } else { (void)sl_bt_connection_close(opened->connection); }
      break;
    }
    case sl_bt_evt_connection_parameters_id:
      if (evt->data.evt_connection_parameters.connection == upstream && phase == WAIT_PARAMETERS) {
        set_phase(DISCOVER_SERVICE);
        (void)procedure(sl_bt_gatt_discover_primary_services_by_uuid(upstream, 16, service_uuid), "services");
      }
      break;
    case sl_bt_evt_gatt_service_id:
      if (evt->data.evt_gatt_service.connection == upstream && phase == DISCOVER_SERVICE) {
        service_handle = evt->data.evt_gatt_service.service;
      }
      break;
    case sl_bt_evt_gatt_characteristic_id: {
      const sl_bt_evt_gatt_characteristic_t *c = &evt->data.evt_gatt_characteristic;
      if (c->connection != upstream || phase != DISCOVER_CHARACTERISTICS || c->uuid.len != 16) { break; }
      if (!memcmp(c->uuid.data, cough_uuid, 16) && (c->properties & 0x10)) { cough_handle = c->characteristic; }
      if (!memcmp(c->uuid.data, environment_uuid, 16) && (c->properties & 0x10)) { environment_handle = c->characteristic; }
      if (!memcmp(c->uuid.data, pendant_uuid, 16) && (c->properties & 0x02)) { pendant_handle = c->characteristic; }
      if (!memcmp(c->uuid.data, ai_uuid, 16) && (c->properties & 0x02)) { ai_handle = c->characteristic; }
      break;
    }
    case sl_bt_evt_gatt_procedure_completed_id:
      if (evt->data.evt_gatt_procedure_completed.connection == upstream) {
        discovery_finished(evt->data.evt_gatt_procedure_completed.result);
      }
      break;
    case sl_bt_evt_gatt_characteristic_value_id: source_value(&evt->data.evt_gatt_characteristic_value); break;
    case sl_bt_evt_gatt_server_user_read_request_id: client_read(&evt->data.evt_gatt_server_user_read_request); break;
    case sl_bt_evt_gatt_server_characteristic_status_id: {
      const sl_bt_evt_gatt_server_characteristic_status_t *s = &evt->data.evt_gatt_server_characteristic_status;
      if (s->connection != gateway) { break; }
      if (s->status_flags == sl_bt_gatt_server_client_config) {
        if (s->characteristic == gattdb_cough_event) { cough_cccd = s->client_config_flags; }
        if (s->characteristic == gattdb_environment_data) {
          environment_cccd = s->client_config_flags; environment_pending = environment.valid;
        }
        if (s->characteristic == gattdb_relay_status) { status_cccd = s->client_config_flags; }
        notify_status();
      } else if (s->status_flags == sl_bt_gatt_server_confirmation
                 && s->characteristic == gattdb_cough_event && indication_pending) {
        indication_pending = false;
        relay_queue_commit(&queue);
      }
      flush(); break;
    }
    case sl_bt_evt_gatt_server_indication_timeout_id:
      if (evt->data.evt_gatt_server_indication_timeout.connection == gateway) {
        app_log("RELAY: Pi confirmation timeout; retaining queue\r\n");
        (void)sl_bt_connection_close(gateway);
      }
      break;
    case sl_bt_evt_connection_closed_id: {
      uint8_t connection = evt->data.evt_connection_closed.connection;
      if (connection == upstream) {
        app_log("RELAY: source disconnected reason=0x%04x\r\n", evt->data.evt_connection_closed.reason);
        reset_upstream();
      }
      if (connection == gateway) {
        gateway = INVALID_CONNECTION;
        gateway_rssi = 127;
        cough_cccd = environment_cccd = status_cccd = 0;
        indication_pending = false; /* Unconfirmed head remains for reconnect. */
        app_log("RELAY: Pi disconnected; retaining %u events\r\n", queue.count);
        start_advertising(); notify_status();
      }
      break;
    }
    default: break;
  }
}
