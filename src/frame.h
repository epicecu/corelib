#ifndef FRAME_H
#define FRAME_H

#include <Arduino.h>

/**
 * @brief Frame Preamble
 */

enum class Preamble : uint16_t {
    NA = 0x00,
    DATA = 0x01,
    PROGRAMMOR_COMPATIBLE_REQUEST = 0x02,
    PROGRAMMOR_COMPATIBLE_RESPONSE = 0x03,
    ARP_REQUEST = 0x04,
    ARP_RESPONSE = 0x05
};


/**
 * The Automotive Frame Protocol(AFP) - struct Frame; is a 64 byte frame to use 
 * over USB HID, Canbus FD or Ethernet to communicate between Epic devices and 
 * tuning software. The device will automatically perform packet forwarding if
 * supported.
 * 
 * The `preamble` states the protocol of the packet. The MSB must be zero (0).
 *  Null = 0x00
 *  Data packet = 0x01
 *  ARP Request packet = 0x02
 *  ARP Response packet = 0x03
 * 
 * The `destinationAddress` denotes the destination device for the packet. 
 *  0x00 = A catch all address, first receiving device to respond
 *  0x01 = PC software Programmor.com
 *  0x02 - 0x7F = A specific device on the local network
 *  
 * The `sourceAddress` denotes the source device of the packet.
 *  0x00 = Reserved
 *  0x01 = PC software Programmor.com
 *  0x02 - 0x7F = A specific device on the local network
 * 
 * The `frameID` denotes an Id which packets belong to, this id may span 
 * multiple frames as the devices will know which packets belong together.
 * 
 * The `frameOrder` denotes the order of a packet to a create a complete
 * message.
 * 
 * The `frameTotal` denotes the total amount of packets requied to complete
 * a message.
 * 
 * The `payload` includes the part or whole of the message data up to 47 bytes
 * for the single packet.
 * 
 * The `crc` includes the checksum of the payload.
 */

struct Frame {
    Preamble preamble = Preamble::NA;
    uint8_t destinationAddress = 0x01;
    uint8_t sourceAddress;
    uint32_t frameID;
    uint8_t frameOrder;
    uint8_t frameTotal;
    uint8_t payload[50] = {0};
    uint32_t crc;
};
#endif // FRAME_H