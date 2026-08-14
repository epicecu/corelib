#include "tusb.h"

#include "pico/unique_id.h"

#include <stdio.h>
#include <string.h>

#define USB_VID 0xcafeu
#define USB_PID 0x4010u
#define USB_BCD 0x0100u
#define INTERFACE_HID 0u
#define ENDPOINT_HID_OUT 0x01u
#define ENDPOINT_HID_IN 0x81u
#define CONFIG_TOTAL_LENGTH (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_BCD,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t hid_report_descriptor[] = {
    0x06, 0x00, 0xff,       /* Usage Page (vendor defined 0xff00). */
    0x09, 0x01,             /* Usage 1. */
    0xa1, 0x01,             /* Application collection. */
    0x15, 0x00,             /* Logical minimum 0. */
    0x26, 0xff, 0x00,       /* Logical maximum 255. */
    0x75, 0x08,             /* Eight bits per item. */
    0x95, 0x40,             /* 64 input bytes. */
    0x09, 0x01,
    0x81, 0x02,             /* Data, variable, absolute input. */
    0x95, 0x40,             /* 64 output bytes. */
    0x09, 0x01,
    0x91, 0x02,             /* Data, variable, absolute output. */
    0xc0,
};

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LENGTH, 0x00, 100),
    TUD_HID_INOUT_DESCRIPTOR(INTERFACE_HID, 0, HID_ITF_PROTOCOL_NONE,
                             sizeof(hid_report_descriptor), ENDPOINT_HID_OUT,
                             ENDPOINT_HID_IN, 64, 1),
};

static const char *const string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "EpicECU",
    "Programmor Pico Test",
    NULL,
};

static uint16_t string_buffer[33];
static char serial_number[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *)&device_descriptor;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return hid_report_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return configuration_descriptor;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language_id) {
  const char *source;
  size_t count;
  size_t position;
  (void)language_id;
  if (index == 0u) {
    memcpy(&string_buffer[1], string_descriptors[0], 2u);
    count = 1u;
  } else {
    if (index == 3u) {
      pico_get_unique_board_id_string(serial_number, sizeof(serial_number));
      source = serial_number;
    } else if (index < 3u) {
      source = string_descriptors[index];
    } else {
      return NULL;
    }
    count = strlen(source);
    if (count > 32u) count = 32u;
    for (position = 0u; position < count; ++position) {
      string_buffer[1u + position] = (uint8_t)source[position];
    }
  }
  string_buffer[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * count + 2u));
  return string_buffer;
}
