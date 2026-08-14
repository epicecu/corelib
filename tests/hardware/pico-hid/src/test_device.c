#include "test_device.h"

#include "vendor/nanopb/pb_decode.h"
#include "vendor/nanopb/pb_encode.h"

#include <string.h>

static bool encode_message(const pb_msgdesc_t *fields, const void *message,
                           uint8_t *output, size_t capacity,
                           size_t *output_size) {
  pb_ostream_t stream;
  if (output == NULL || output_size == NULL) return false;
  stream = pb_ostream_from_buffer(output, capacity);
  if (!pb_encode(&stream, fields, message)) return false;
  *output_size = stream.bytes_written;
  return true;
}

static bool decode_message(const pb_msgdesc_t *fields, void *message,
                           const uint8_t *data, size_t data_size) {
  pb_istream_t stream;
  if (data_size != 0u && data == NULL) return false;
  stream = pb_istream_from_buffer(data, data_size);
  return pb_decode(&stream, fields, message) && stream.bytes_left == 0u;
}

void corelib_test_device_init(corelib_test_device_t *device,
                                 uint64_t now_ms) {
  if (device == NULL) return;
  memset(device, 0, sizeof(*device));
  device->common.id = 2u;
  device->common.registry_id = 1001u;
  device->common.serial_number = 123u;
  device->common.shares_version = 1u;
  device->common.firmware_version = 202308u;
  strcpy(device->common.device_name, "Test Device A");
  device->share1.ending_number = 20;
  device->share2.frequency_input_pin_id = 101;
  device->share2.digital_output_pin_id = 102;
  device->share2.analog_input_a_pin_id = 103;
  device->share2.analog_input_b_pin_id = 104;
  device->share3.loops_per_second = 10000;
  strcpy(device->share4.welcome_text, "Hello there!");
  device->share5.float_number = 23.45f;
  device->share5.double_number = 1.2345678910111213;
  strcpy(device->share5.ip_address, "192.168.1.5");
  device->share5.port_number = 8080;
  strcpy(device->share5.date_time, "2023-08-01T00:00:00Z");
  device->share5.boolean_value = true;
  device->share6.day_of_the_week = 1;
  device->next_counter_ms = now_ms + 1000u;
}

void corelib_test_device_tick(corelib_test_device_t *device,
                                 uint64_t now_ms) {
  if (device == NULL) return;
  while (now_ms >= device->next_counter_ms) {
    ++device->share1.counter;
    if (device->share1.counter > device->share1.ending_number) {
      device->share1.counter = device->share1.starting_number;
    }
    device->next_counter_ms += 1000u;
  }
}

bool corelib_test_device_encode(const corelib_test_device_t *device,
                                   bool common, uint32_t share_id,
                                   uint8_t *output, size_t capacity,
                                   size_t *output_size) {
  if (device == NULL || output == NULL || output_size == NULL) return false;
  if (common) {
    return share_id == 1u &&
           encode_message(epicecu_programmor_test_device_v1_Common1_fields,
                          &device->common, output, capacity, output_size);
  }
  switch (share_id) {
    case 1u:
      return encode_message(epicecu_programmor_test_device_v1_Share1_fields,
                            &device->share1, output, capacity, output_size);
    case 2u:
      return encode_message(epicecu_programmor_test_device_v1_Share2_fields,
                            &device->share2, output, capacity, output_size);
    case 3u:
      return encode_message(epicecu_programmor_test_device_v1_Share3_fields,
                            &device->share3, output, capacity, output_size);
    case 4u:
      return encode_message(epicecu_programmor_test_device_v1_Share4_fields,
                            &device->share4, output, capacity, output_size);
    case 5u:
      return encode_message(epicecu_programmor_test_device_v1_Share5_fields,
                            &device->share5, output, capacity, output_size);
    case 6u:
      return encode_message(epicecu_programmor_test_device_v1_Share6_fields,
                            &device->share6, output, capacity, output_size);
    default:
      return false;
  }
}

bool corelib_test_device_publish(corelib_test_device_t *device,
                                    bool common, uint32_t share_id,
                                    const uint8_t *data, size_t data_size) {
  if (device == NULL || common) return false;
  switch (share_id) {
    case 1u: {
      epicecu_programmor_test_device_v1_Share1 value =
          epicecu_programmor_test_device_v1_Share1_init_zero;
      if (!decode_message(epicecu_programmor_test_device_v1_Share1_fields,
                          &value, data, data_size) ||
          value.starting_number > value.ending_number) return false;
      device->share1.starting_number = value.starting_number;
      device->share1.ending_number = value.ending_number;
      if (device->share1.counter < value.starting_number ||
          device->share1.counter > value.ending_number) {
        device->share1.counter = value.starting_number;
      }
      return true;
    }
    case 2u: {
      epicecu_programmor_test_device_v1_Share2 value =
          epicecu_programmor_test_device_v1_Share2_init_zero;
      if (!decode_message(epicecu_programmor_test_device_v1_Share2_fields,
                          &value, data, data_size)) return false;
      device->share2 = value;
      return true;
    }
    case 4u: {
      epicecu_programmor_test_device_v1_Share4 value =
          epicecu_programmor_test_device_v1_Share4_init_zero;
      if (!decode_message(epicecu_programmor_test_device_v1_Share4_fields,
                          &value, data, data_size)) return false;
      device->share4 = value;
      return true;
    }
    case 5u: {
      epicecu_programmor_test_device_v1_Share5 value =
          epicecu_programmor_test_device_v1_Share5_init_zero;
      if (!decode_message(epicecu_programmor_test_device_v1_Share5_fields,
                          &value, data, data_size)) return false;
      device->share5 = value;
      return true;
    }
    case 6u: {
      epicecu_programmor_test_device_v1_Share6 value =
          epicecu_programmor_test_device_v1_Share6_init_zero;
      if (!decode_message(epicecu_programmor_test_device_v1_Share6_fields,
                          &value, data, data_size) ||
          value.day_of_the_week < 1 || value.day_of_the_week > 7) return false;
      device->share6 = value;
      return true;
    }
    default:
      return false;
  }
}
