#include "corelib_self_test.h"
#include "test_device.h"

#include "vendor/nanopb/pb_encode.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t encode(const pb_msgdesc_t *fields, const void *message,
                     uint8_t *output, size_t capacity) {
  pb_ostream_t stream = pb_ostream_from_buffer(output, capacity);
  assert(pb_encode(&stream, fields, message));
  return stream.bytes_written;
}

int main(void) {
  static const uint8_t expected_common[] = {
      0x0d, 0x02, 0x00, 0x00, 0x00, 0x15, 0xe9, 0x03, 0x00, 0x00,
      0x1d, 0x7b, 0x00, 0x00, 0x00, 0x25, 0x01, 0x00, 0x00, 0x00,
      0x2d, 0x44, 0x16, 0x03, 0x00, 0x32, 0x0d, 'T', 'e', 's',
      't', ' ', 'D', 'e', 'v', 'i', 'c', 'e', ' ', 'A'};
  corelib_test_device_t device;
  epicecu_programmor_test_device_v1_Share1 range =
      epicecu_programmor_test_device_v1_Share1_init_zero;
  epicecu_programmor_test_device_v1_Share4 text =
      epicecu_programmor_test_device_v1_Share4_init_zero;
  uint8_t encoded[128];
  size_t encoded_size = 0u;
  const uint8_t self_test = corelib_self_test();

  if (self_test != 0u)
    fprintf(stderr, "Pico SDK self-test failure: %u\n", self_test);
  assert(self_test == 0u);

  corelib_test_device_init(&device, 100u);
  assert(corelib_test_device_encode(&device, true, 1u, encoded,
                                    sizeof(encoded), &encoded_size));
  assert(encoded_size == sizeof(expected_common));
  assert(memcmp(encoded, expected_common, sizeof(expected_common)) == 0);
  assert(!corelib_test_device_encode(&device, true, 2u, encoded,
                                     sizeof(encoded), &encoded_size));

  corelib_test_device_tick(&device, 3100u);
  assert(device.share1.counter == 3);
  range.starting_number = 5;
  range.ending_number = 6;
  encoded_size = encode(epicecu_programmor_test_device_v1_Share1_fields,
                        &range, encoded, sizeof(encoded));
  assert(corelib_test_device_publish(&device, false, 1u, encoded,
                                     encoded_size));
  assert(device.share1.counter == 5);
  corelib_test_device_tick(&device, 4100u);
  corelib_test_device_tick(&device, 5100u);
  assert(device.share1.counter == 5);

  strcpy(text.welcome_text, "Pico updated");
  encoded_size = encode(epicecu_programmor_test_device_v1_Share4_fields,
                        &text, encoded, sizeof(encoded));
  assert(corelib_test_device_publish(&device, false, 4u, encoded,
                                     encoded_size));
  assert(strcmp(device.share4.welcome_text, "Pico updated") == 0);
  assert(!corelib_test_device_publish(&device, true, 1u, encoded,
                                      encoded_size));
  assert(!corelib_test_device_publish(&device, false, 3u, encoded,
                                      encoded_size));
  assert(!corelib_test_device_publish(&device, false, 99u, encoded,
                                      encoded_size));

  puts("corelib_pico_device_tests: ok");
  return 0;
}
