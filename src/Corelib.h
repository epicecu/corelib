/**
 * @file Corelib.h
 * @brief Arduino-compatible umbrella include for the C and C++ device APIs.
 */
#ifndef CORELIB_DEVICE_ARDUINO_H
#define CORELIB_DEVICE_ARDUINO_H

#include "corelib/device.h"

#ifdef CORELIB_ENABLE_GATEWAY
#include "corelib/gateway.h"
#endif

#ifdef __cplusplus
#ifdef ARDUINO
#include <Embedded_Template_Library.h>
#endif
#include "corelib/device.hpp"
#ifdef CORELIB_ENABLE_GATEWAY
#include "corelib/gateway.hpp"
#endif
#endif

#endif
