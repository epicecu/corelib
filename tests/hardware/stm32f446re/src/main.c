#include <corelib/device.h>
#include <stm32f4xx_hal.h>
#include <stdalign.h>
#include <string.h>

enum { MESSAGE_BYTES = 512, REASSEMBLY_SLOTS = 2, OUTBOUND_FRAMES = 16, PENDING_REQUESTS = 4 };
static alignas(max_align_t) uint8_t context_memory[CORELIB_CONTEXT_STORAGE_SIZE];
static alignas(max_align_t) uint8_t pending[PENDING_REQUESTS * CORELIB_PENDING_REQUEST_STORAGE_SIZE];
static uint8_t messages[REASSEMBLY_SLOTS * MESSAGE_BYTES];
static uint8_t received[REASSEMBLY_SLOTS * 255];
static uint8_t scratch[MESSAGE_BYTES];
static uint8_t outbound[OUTBOUND_FRAMES * CORELIB_FRAME_SIZE];

static corelib_send_result_t send_frame(void *user, corelib_link_id_t link_id,
    void *transport_context, const uint8_t frame[CORELIB_FRAME_SIZE]) {
  (void)user; (void)link_id; (void)transport_context; (void)frame;
  return CORELIB_SEND_BUSY;
}

void SysTick_Handler(void) { HAL_IncTick(); }

int main(void) {
  corelib_config_t config;
  corelib_context_t *device = NULL;
  HAL_Init();
  memset(&config, 0, sizeof(config));
  config.node_uuid[0] = 0x12; config.node_uuid[6] = 0x40; config.node_uuid[8] = 0x80;
  config.heartbeat_interval_ms = 2000;
  config.application_response_timeout_ms = 1000;
  config.maximum_transaction_data_size = 256;
  config.callbacks.send_frame = send_frame;
  config.storage.reassembly.message = messages;
  config.storage.reassembly.received = received;
  config.storage.reassembly_slot_count = REASSEMBLY_SLOTS;
  config.storage.maximum_message_size = MESSAGE_BYTES;
  config.storage.transaction_scratch = scratch;
  config.storage.outbound.frames = outbound;
  config.storage.outbound.capacity = OUTBOUND_FRAMES;
  config.storage.pending_requests.entries = pending;
  config.storage.pending_requests.capacity = PENDING_REQUESTS;
  config.storage.pending_requests.entry_size = CORELIB_PENDING_REQUEST_STORAGE_SIZE;
  if (corelib_init(context_memory, sizeof(context_memory), &config, &device) != CORELIB_OK ||
      corelib_add_link(device, 1, NULL) != CORELIB_OK) return 1;
  for (;;) (void)corelib_tick(device, HAL_GetTick());
}
