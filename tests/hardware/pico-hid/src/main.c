#include "corelib/device.h"
#include "corelib_self_test.h"
#include "test_device.h"

#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "bsp/board.h"
#include "tusb.h"

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LED_PIN 25u
#define LINK_ID 1u
#define MAX_MESSAGE 512u
#define REASSEMBLY_SLOTS 2u
#define OUTBOUND_FRAMES 16u
#define PENDING_REQUESTS 4u
#define RX_FRAMES 4u

typedef struct {
  corelib_transaction_id_t items[PENDING_REQUESTS];
  size_t head;
  size_t count;
} request_queue_t;

static alignas(max_align_t)
    uint8_t context_memory[CORELIB_CONTEXT_STORAGE_SIZE];
static alignas(max_align_t)
    uint8_t pending_entries[PENDING_REQUESTS *
                            CORELIB_PENDING_REQUEST_STORAGE_SIZE];
static uint8_t messages[MAX_MESSAGE * REASSEMBLY_SLOTS];
static uint8_t received[255u * REASSEMBLY_SLOTS];
static uint8_t transaction_scratch[MAX_MESSAGE];
static uint8_t outbound[CORELIB_FRAME_SIZE * OUTBOUND_FRAMES];
static uint8_t rx_frames[RX_FRAMES][CORELIB_FRAME_SIZE];
static volatile size_t rx_head;
static volatile size_t rx_count;
static corelib_context_t *device;
static corelib_test_device_t device;
static request_queue_t requests;

static corelib_send_result_t send_frame(
    void *user, corelib_link_id_t link_id, void *transport_context,
    const uint8_t frame[CORELIB_FRAME_SIZE]) {
  (void)user;
  (void)transport_context;
  if (link_id != LINK_ID || !tud_hid_ready()) return CORELIB_SEND_BUSY;
  return tud_hid_report(0u, frame, CORELIB_FRAME_SIZE)
             ? CORELIB_SEND_ACCEPTED
             : CORELIB_SEND_BUSY;
}

static void transaction_received(void *user,
                                 const corelib_transaction_t *value) {
  const bool common = value->id.action == CORELIB_ACTION_COMMON_PUBLISH;
  (void)user;
  if (value->id.action == CORELIB_ACTION_COMMON_PUBLISH ||
      value->id.action == CORELIB_ACTION_SHARE_PUBLISH) {
    (void)corelib_test_device_publish(&device, common, value->id.share_id,
                                         value->data, value->data_size);
    return;
  }
  if (requests.count < PENDING_REQUESTS) {
    const size_t slot = (requests.head + requests.count) % PENDING_REQUESTS;
    requests.items[slot] = value->id;
    ++requests.count;
  }
}

static bool init_sdk(void) {
  corelib_config_t config;
  memset(&config, 0, sizeof(config));
  config.node_uuid[0] = 0x40u;
  config.node_uuid[6] = 0x40u;
  config.node_uuid[8] = 0x80u;
  config.node_uuid[15] = 1u;
  config.heartbeat_interval_ms = 2000u;
  config.application_response_timeout_ms = 1000u;
  config.maximum_transaction_data_size = 256u;
  config.callbacks.send_frame = send_frame;
  config.callbacks.transaction = transaction_received;
  config.storage.reassembly.message = messages;
  config.storage.reassembly.received = received;
  config.storage.reassembly_slot_count = REASSEMBLY_SLOTS;
  config.storage.maximum_message_size = MAX_MESSAGE;
  config.storage.transaction_scratch = transaction_scratch;
  config.storage.outbound.frames = outbound;
  config.storage.outbound.capacity = OUTBOUND_FRAMES;
  config.storage.pending_requests.entries = pending_entries;
  config.storage.pending_requests.capacity = PENDING_REQUESTS;
  config.storage.pending_requests.entry_size =
      CORELIB_PENDING_REQUEST_STORAGE_SIZE;
  return corelib_init(context_memory, sizeof(context_memory), &config,
                             &device) == CORELIB_OK &&
         corelib_add_link(device, LINK_ID, NULL) == CORELIB_OK;
}

static void service_request(void) {
  corelib_transaction_id_t request;
  corelib_transaction_result_t result = CORELIB_RESULT_UNSUPPORTED;
  uint8_t payload[128];
  size_t payload_size = 0u;
  bool common;
  if (requests.count == 0u) return;
  request = requests.items[requests.head];
  requests.head = (requests.head + 1u) % PENDING_REQUESTS;
  --requests.count;
  common = request.action == CORELIB_ACTION_COMMON_REQUEST;
  if (corelib_test_device_encode(&device, common, request.share_id, payload,
                                    sizeof(payload), &payload_size)) {
    result = CORELIB_RESULT_SUCCESS;
  }
  (void)corelib_respond(device, &request, result,
                              result == CORELIB_RESULT_SUCCESS ? payload
                                                                      : NULL,
                              result == CORELIB_RESULT_SUCCESS
                                  ? payload_size
                                  : 0u);
}

static void signal_self_test_pass(void) {
  unsigned pulse;
  for (pulse = 0u; pulse < 3u; ++pulse) {
    gpio_put(LED_PIN, true);
    sleep_ms(100u);
    gpio_put(LED_PIN, false);
    sleep_ms(100u);
  }
}

static void signal_self_test_failure(uint8_t code) {
  for (;;) {
    uint8_t pulse;
    for (pulse = 0u; pulse < code; ++pulse) {
      gpio_put(LED_PIN, true);
      sleep_ms(150u);
      gpio_put(LED_PIN, false);
      sleep_ms(150u);
    }
    sleep_ms(1000u);
  }
}

int main(void) {
  uint8_t self_test;
  board_init();
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
  self_test = corelib_self_test();
  if (self_test != 0u) signal_self_test_failure(self_test);
  signal_self_test_pass();
  corelib_test_device_init(&device, to_ms_since_boot(get_absolute_time()));
  if (!init_sdk()) signal_self_test_failure(8u);
  tusb_init();
  for (;;) {
    uint8_t frame[CORELIB_FRAME_SIZE];
    const uint64_t now_ms = to_ms_since_boot(get_absolute_time());
    tud_task();
    if (rx_count != 0u) {
      const uint32_t interrupt_state = save_and_disable_interrupts();
      memcpy(frame, rx_frames[rx_head], sizeof(frame));
      rx_head = (rx_head + 1u) % RX_FRAMES;
      --rx_count;
      restore_interrupts(interrupt_state);
      (void)corelib_receive_frame(device, LINK_ID, frame, now_ms);
    }
    corelib_test_device_tick(&device, now_ms);
    service_request();
    (void)corelib_tick(device, now_ms);
  }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t requested_length) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)requested_length;
  return 0u;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t buffer_size) {
  size_t slot;
  (void)instance;
  (void)report_id;
  if (report_type != HID_REPORT_TYPE_OUTPUT ||
      buffer_size != CORELIB_FRAME_SIZE || rx_count >= RX_FRAMES) return;
  slot = (rx_head + rx_count) % RX_FRAMES;
  memcpy(rx_frames[slot], buffer, CORELIB_FRAME_SIZE);
  ++rx_count;
}
