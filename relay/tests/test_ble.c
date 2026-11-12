/* Exercise the real relay event handler against SDK event types and mocked radio. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sl_bt_api.h"
#include "gatt_db.h"
static uint32_t test_time;
static sl_status_t send_result = SL_STATUS_OK;
static unsigned indication_count, close_count;
static uint8_t opened_phy, preferred_phy, scanning_phys;
static uint8_t sent_packet[8], read_error;
static uint16_t read_size;
static uint8_t read_data[20];
static sl_status_t send_indication(uint8_t c, uint16_t h, size_t n, const uint8_t *p)
{
  assert(c == 2 && h == gattdb_cough_event && n == 8);
  if (send_result == SL_STATUS_OK) { ++indication_count; memcpy(sent_packet, p, n); }
  return send_result;
}
static sl_status_t read_response(uint8_t c, uint16_t h, uint8_t err, size_t n,
                                 const uint8_t *p, uint16_t *sent)
{
  (void)c; (void)h; (void)sent;
  read_error = err; read_size = (uint16_t)n;
  assert(n <= sizeof(read_data));
  if (n) { memcpy(read_data, p, n); }
  return SL_STATUS_OK;
}
#define sl_bt_advertiser_create_set(out) (*(out) = 0, SL_STATUS_OK)
#define sl_bt_advertiser_set_timing(...) SL_STATUS_OK
#define sl_bt_connection_close(...) (++close_count, SL_STATUS_OK)
#define sl_bt_connection_get_median_rssi(c, out) (*(out) = -50, SL_STATUS_OK)
#define sl_bt_connection_open(a, t, p, out) (opened_phy = (p), *(out) = 1, SL_STATUS_OK)
#define sl_bt_connection_set_preferred_phy(c, p, a) (preferred_phy = (p), SL_STATUS_OK)
#define sl_bt_system_set_tx_power(a, b, c, d) (*(c) = (a), *(d) = (b), SL_STATUS_OK)
#define sl_bt_connection_set_default_parameters(...) SL_STATUS_OK
#define sl_bt_external_signal(...) SL_STATUS_OK
#define sl_bt_gatt_discover_characteristics(...) SL_STATUS_OK
#define sl_bt_gatt_discover_primary_services_by_uuid(...) SL_STATUS_OK
#define sl_bt_gatt_read_characteristic_value(...) SL_STATUS_OK
#define sl_bt_gatt_send_characteristic_confirmation(...) SL_STATUS_OK
#define sl_bt_gatt_server_send_indication send_indication
#define sl_bt_gatt_server_send_notification(...) send_result
#define sl_bt_gatt_server_send_user_read_response read_response
#define sl_bt_gatt_server_write_attribute_value(...) SL_STATUS_OK
#define sl_bt_gatt_set_characteristic_notification(...) SL_STATUS_OK
#define sl_bt_legacy_advertiser_generate_data(...) SL_STATUS_OK
#define sl_bt_legacy_advertiser_start(...) SL_STATUS_OK
#define sl_bt_scanner_set_parameters(...) SL_STATUS_OK
#define sl_bt_scanner_start(p, m) (scanning_phys = (p), SL_STATUS_OK)
#define sl_bt_scanner_stop(...) SL_STATUS_OK
#include "../relay_ble.c"

static sl_bt_msg_t event;
static void emit(uint32_t id) { event.header = id; relay_on_event(&event); memset(&event, 0, sizeof(event)); }
static void complete(void) { event.data.evt_gatt_procedure_completed.connection = 1; emit(sl_bt_evt_gatt_procedure_completed_id); }
static void discover(uint16_t handle, const uint8_t uuid[16])
{
  event.data.evt_gatt_characteristic.connection = 1;
  event.data.evt_gatt_characteristic.characteristic = handle;
  event.data.evt_gatt_characteristic.properties = 0x12;
  event.data.evt_gatt_characteristic.uuid.len = 16;
  memcpy(event.data.evt_gatt_characteristic.uuid.data, uuid, 16);
  emit(sl_bt_evt_gatt_characteristic_id);
}
static void cough(uint8_t connection, uint8_t size)
{
  event.data.evt_gatt_characteristic_value.connection = connection;
  event.data.evt_gatt_characteristic_value.characteristic = 50;
  event.data.evt_gatt_characteristic_value.att_opcode = sl_bt_gatt_handle_value_notification;
  event.data.evt_gatt_characteristic_value.value.len = size;
  memcpy(event.data.evt_gatt_characteristic_value.value.data, "12345678", size);
  emit(sl_bt_evt_gatt_characteristic_value_id);
}
static void open_gateway(void)
{
  event.data.evt_connection_opened.connection = 2;
  event.data.evt_connection_opened.role = sl_bt_connection_role_peripheral;
  emit(sl_bt_evt_connection_opened_id);
  event.data.evt_gatt_server_characteristic_status.connection = 2;
  event.data.evt_gatt_server_characteristic_status.characteristic = gattdb_cough_event;
  event.data.evt_gatt_server_characteristic_status.status_flags = sl_bt_gatt_server_client_config;
  event.data.evt_gatt_server_characteristic_status.client_config_flags = sl_bt_gatt_indication;
  emit(sl_bt_evt_gatt_server_characteristic_status_id);
}
static void confirm(uint8_t connection)
{
  event.data.evt_gatt_server_characteristic_status.connection = connection;
  event.data.evt_gatt_server_characteristic_status.characteristic = gattdb_cough_event;
  event.data.evt_gatt_server_characteristic_status.status_flags = sl_bt_gatt_server_confirmation;
  emit(sl_bt_evt_gatt_server_characteristic_status_id);
}
int main(void)
{
  emit(sl_bt_evt_system_boot_id);
  assert(scanning && advertising && phase == DOWN);
  assert(scanning_phys == (sl_bt_gap_phy_1m | sl_bt_gap_phy_coded));
  /* Non-connectable and foreign reports cannot hijack the paired source. */
  event.data.evt_scanner_extended_advertisement_report.address = source_address;
  event.data.evt_scanner_extended_advertisement_report.primary_phy = sl_bt_gap_phy_coded;
  emit(sl_bt_evt_scanner_extended_advertisement_report_id);
  assert(phase == DOWN);
  event.data.evt_scanner_extended_advertisement_report.event_flags = 1;
  emit(sl_bt_evt_scanner_extended_advertisement_report_id);
  assert(phase == DOWN);
  event.data.evt_scanner_extended_advertisement_report.address = source_address;
  event.data.evt_scanner_extended_advertisement_report.primary_phy = sl_bt_gap_phy_coded;
  event.data.evt_scanner_extended_advertisement_report.event_flags = 1;
  emit(sl_bt_evt_scanner_extended_advertisement_report_id);
  assert(phase == CONNECTING && upstream == 1 && !scanning && opened_phy == sl_bt_gap_phy_coded);
  event.data.evt_connection_opened.connection = 1;
  event.data.evt_connection_opened.role = sl_bt_connection_role_central;
  emit(sl_bt_evt_connection_opened_id);
  assert(phase == WAIT_PARAMETERS && preferred_phy == 4u);
  event.data.evt_connection_parameters.connection = 1;
  emit(sl_bt_evt_connection_parameters_id);
  assert(phase == DISCOVER_SERVICE);
  event.data.evt_gatt_service.connection = 1;
  event.data.evt_gatt_service.service = 10;
  emit(sl_bt_evt_gatt_service_id); complete();
  discover(50,cough_uuid); discover(51,environment_uuid);
  discover(52,pendant_uuid); discover(53,ai_uuid);
  complete(); assert(phase == SUBSCRIBE_COUGH && source_bound);
  complete(); assert(phase == SUBSCRIBE_ENVIRONMENT);
  complete(); assert(phase == RUNNING && upstream_ready);
  cough(3,8); assert(queue.count == 0); /* Ignore other connections. */
  cough(1,7); assert(queue.count == 0 && invalid_packets == 1);
  cough(1,8); assert(queue.count == 1 && queue.delivered == 0);
  send_result = SL_STATUS_NO_MORE_RESOURCE;
  open_gateway(); assert(queue.count == 1 && !indication_pending);
  send_result = SL_STATUS_OK;
  flush(); assert(indication_pending && queue.count == 1 && indication_count == 1);
  flush(); assert(indication_count == 1); /* No duplicate while awaiting ACK. */
  confirm(1); assert(queue.count == 1); /* ACK from wrong connection ignored. */
  confirm(2); assert(queue.count == 0 && queue.delivered == 1);
  confirm(2); assert(queue.delivered == 1);
  cough(1,8); assert(queue.count == 1 && indication_pending);
  event.data.evt_connection_closed.connection = 2; emit(sl_bt_evt_connection_closed_id);
  assert(queue.count == 1 && !indication_pending);
  open_gateway(); assert(indication_pending && !memcmp(sent_packet,"12345678",8));
  confirm(2); assert(queue.count == 0 && queue.delivered == 2);
  event.data.evt_connection_closed.connection = 1; emit(sl_bt_evt_connection_closed_id);
  assert(!upstream_ready && phase == DOWN && source_bound);
  event.data.evt_gatt_server_user_read_request.connection = 2;
  event.data.evt_gatt_server_user_read_request.characteristic = gattdb_pendant_status;
  emit(sl_bt_evt_gatt_server_user_read_request_id);
  assert(read_error == 14 && read_size == 0); /* Never serve stale sensor data. */
  event.data.evt_gatt_server_user_read_request.connection = 2;
  event.data.evt_gatt_server_user_read_request.characteristic = gattdb_relay_status;
  event.data.evt_gatt_server_user_read_request.offset = 21;
  emit(sl_bt_evt_gatt_server_user_read_request_id);
  assert(read_error == 7 && read_size == 0);
  test_time += RELAY_RETRY_MS;
  event.data.evt_system_external_signal.extsignals = TICK_SIGNAL;
  emit(sl_bt_evt_system_external_signal_id);
  assert(scanning);
  puts("PASS: discovery, routing, backpressure, ACK, reconnect, stale read, rescan");
  return 0;
}
