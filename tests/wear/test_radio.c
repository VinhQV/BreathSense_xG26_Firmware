/* Execute production BLE handler: sleep/wake races must never leave radio on. */
#include <assert.h>
#include <string.h>
#include "sl_bt_api.h"
static unsigned adv_starts, adv_stops, closes;
static uint32_t signals;
uint64_t test_ticks;
static uint8_t write_error;
#define sl_bt_advertiser_create_set(h) (*(h)=0, SL_STATUS_OK)
#define sl_bt_advertiser_set_timing(...) SL_STATUS_OK
#define sl_bt_advertiser_stop(...) (++adv_stops, SL_STATUS_OK)
#define sl_bt_extended_advertiser_generate_data(...) SL_STATUS_OK
#define sl_bt_extended_advertiser_set_phy(...) SL_STATUS_OK
#define sl_bt_extended_advertiser_start(...) (++adv_starts, SL_STATUS_OK)
#define sl_bt_connection_close(...) (++closes, SL_STATUS_OK)
#define sl_bt_connection_set_default_preferred_phy(...) SL_STATUS_OK
#define sl_bt_system_set_tx_power(a,b,c,d) (*(c)=(a), *(d)=(b), SL_STATUS_OK)
#define sl_bt_external_signal(s) (signals |= (s), SL_STATUS_OK)
#define sl_bt_gatt_server_send_notification(...) SL_STATUS_OK
#define sl_bt_gatt_server_send_user_read_response(...) SL_STATUS_OK
#define sl_bt_gatt_server_send_user_write_response(c,h,e) (write_error=(e),SL_STATUS_OK)
#include "../../breathsense_ble_advertising.c"
void breathsense_ai_status(uint8_t *d) { memset(d,0,BREATHSENSE_AI_STATUS_SIZE); }
void breathsense_pendant_status(uint8_t *d) { memset(d,0,BREATHSENSE_PENDANT_STATUS_SIZE); }
static sl_bt_msg_t event;
static void emit(uint32_t id) { event.header=id; sl_bt_on_event(&event); memset(&event,0,sizeof(event)); }
static void dispatch(void) { event.data.evt_system_external_signal.extsignals=signals; signals=0; emit(sl_bt_evt_system_external_signal_id); }
int main(void)
{
  breathsense_time_sync_init();
  breathsense_ble_advertising_init();
  emit(sl_bt_evt_system_boot_id); assert(adv_starts==0 && !advertising_active);
  breathsense_ble_set_paused(false); dispatch(); assert(adv_starts==1 && advertising_active);
  breathsense_ble_set_paused(true); dispatch(); assert(adv_stops==1 && !advertising_active);
  /* Connection opened racing with sleep must close immediately. */
  event.data.evt_connection_opened.connection=1; emit(sl_bt_evt_connection_opened_id);
  assert(closes==1 && !breathsense_ble_advertising_is_ready());
  emit(sl_bt_evt_connection_closed_id); assert(adv_starts==1);
  /* Last desired state wins if control signals coalesce. */
  breathsense_ble_set_paused(false); breathsense_ble_set_paused(true); dispatch(); assert(adv_starts==1);
  breathsense_ble_set_paused(false); dispatch(); assert(adv_starts==2);
  event.data.evt_connection_opened.connection=2; emit(sl_bt_evt_connection_opened_id);
  event.data.evt_gatt_server_characteristic_status.connection=2;
  event.data.evt_gatt_server_characteristic_status.characteristic=gattdb_cough_event;
  event.data.evt_gatt_server_characteristic_status.status_flags=sl_bt_gatt_server_client_config;
  event.data.evt_gatt_server_characteristic_status.client_config_flags=sl_bt_gatt_server_notification;
  emit(sl_bt_evt_gatt_server_characteristic_status_id);
  assert(!breathsense_ble_advertising_is_ready()); /* Subscribe is insufficient before UTC. */
  event.data.evt_gatt_server_user_write_request.connection=2;
  event.data.evt_gatt_server_user_write_request.characteristic=gattdb_time_sync;
  event.data.evt_gatt_server_user_write_request.value.len=4;
  emit(sl_bt_evt_gatt_server_user_write_request_id);
  assert(write_error==0x13 && !breathsense_ble_advertising_is_ready());
  event.data.evt_gatt_server_user_write_request.connection=2;
  event.data.evt_gatt_server_user_write_request.characteristic=gattdb_time_sync;
  event.data.evt_gatt_server_user_write_request.value.len=4;
  encode_uint32_le(event.data.evt_gatt_server_user_write_request.value.data,1794452400u);
  emit(sl_bt_evt_gatt_server_user_write_request_id);
  assert(write_error==0);
  assert(breathsense_ble_advertising_is_ready());
  breathsense_ble_set_paused(true);
  assert(!breathsense_ble_advertising_is_ready()); /* Immediate send gate before async close. */
  dispatch(); assert(closes==2);
  emit(sl_bt_evt_connection_closed_id); assert(adv_starts==2);
  test_ticks += (uint64_t)300 * 32768u;
  uint32_t epoch;
  assert(breathsense_time_sync_get_epoch(&epoch) && epoch==1794452700u);
  assert(!time_synchronized_for_connection); /* Retained time is not a new link ACK. */
  breathsense_ble_set_paused(false); dispatch(); assert(adv_starts==3);
  puts("PASS: radio sleep/wake, connection race, subscribe + UTC gate, invalid clock, retained UTC");
}
