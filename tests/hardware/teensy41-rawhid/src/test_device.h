#ifndef CORELIB_PICO_TEST_DEVICE_H
#define CORELIB_PICO_TEST_DEVICE_H

#include "generated/common.pb.h"
#include "generated/shares.pb.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  epicecu_programmor_test_device_v1_Common1 common;
  epicecu_programmor_test_device_v1_Share1 share1;
  epicecu_programmor_test_device_v1_Share2 share2;
  epicecu_programmor_test_device_v1_Share3 share3;
  epicecu_programmor_test_device_v1_Share4 share4;
  epicecu_programmor_test_device_v1_Share5 share5;
  epicecu_programmor_test_device_v1_Share6 share6;
  uint64_t next_counter_ms;
} corelib_test_device_t;

void corelib_test_device_init(corelib_test_device_t *device,
                                 uint64_t now_ms);
void corelib_test_device_tick(corelib_test_device_t *device,
                                 uint64_t now_ms);
bool corelib_test_device_encode(const corelib_test_device_t *device,
                                   bool common, uint32_t share_id,
                                   uint8_t *output, size_t capacity,
                                   size_t *output_size);
bool corelib_test_device_publish(corelib_test_device_t *device,
                                    bool common, uint32_t share_id,
                                    const uint8_t *data, size_t data_size);

#endif
