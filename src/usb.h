#ifndef USB_H
#define USB_H

#include "comm.h"

namespace corelib {

#if defined (NATIVE)
/**
 * @brief Native Unit Test Helpers
 */
static uint8_t usb_sendBuffer[192];
static uint8_t usb_sendBufferIndex = 0;
static uint8_t usb_recvBuffer[64];
static int usb_rawhid_send(const void *buffer, uint32_t timeout)
{
    (void) timeout;
    memcpy(usb_sendBuffer+usb_sendBufferIndex, buffer, 64);
    usb_sendBufferIndex += 64;
    return 64;
}
static int usb_rawhid_recv(void *buffer, uint32_t timeout)
{
    (void) timeout;
    memcpy(buffer, usb_recvBuffer, 64);
    return 64;
}
#endif

/**
 * @brief USB (RAW-HID mode) - Comms Implementation 
 * 
 */
class USB : public Comm {
public:
    
    /**
     * @brief Construct a new USB object
     */
    USB();

protected:
    // Function.h interface
    void performInitilise() {
        // initialise the serial communication:
        #if defined (STM32)
        // todo: Do we need to initilise the hid interface?
        //Serial.begin(board->serialUSBSpeed); // Teensy does not require begin to be called, serial starts automatically at maximum speed.
        #endif
        initilised = true;
    }

    // Comms.h interface
    bool read(uint8_t* buffer) {
        return (usb_rawhid_recv(buffer, 1) > 0);
    }
    bool write(const uint8_t* buffer){
        return (usb_rawhid_send(buffer, 1) > 0);
    }
};
} // NAMESPACE
#endif // USB_H
