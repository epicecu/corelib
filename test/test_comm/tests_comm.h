/**
 * @file tests_commns.h
 *
 * @brief Tests the Comms proto implementation. The unit tests do not cover every share message
 * but rather the main functions including the request and publish modes.
 *
 * @author David Cedar - david@epicecu.com
 */
#include <Arduino.h>
#include <unity.h>

#include "comm.h"

// Mocks the USB buffer
extern uint8_t sendBuffer[192];
extern uint8_t sendBufferIndex;
extern uint8_t recvBuffer[64];

class TestComm : public Comm
{
  public:
    TestComm() : Comm() {
      // do nothing
    }

    auto testProcessRead(){
      return processRead();
    }

    auto testProcessWrite(){
      return processWrite();
    }

    void testProcessIncomingMessage(){
      processIncomingMessage();
    }

    void testProcessOutgoingMessage(){
      processOutgoingMessage();
    }

    auto getBuffer(){
      return buffer;
    }

    void setBuffer(Buffer b){
      buffer = b;
    } 

    auto getInFrames(){
      return inFrames;
    }

    void testReset(){
      inFrames.clear();
      buffer.inIndex = 0;
      buffer.inMessageLength = 0;
      buffer.outMessageLength = 0;
      sendBufferIndex = 0;
    }

  protected:
    // Function.h interface
    void performInitilise(){
      initilised = true;
    };
    void performIterate(){
      // do nothing
    };

    // Comms.h interface
    bool read(uint8_t* buffer){
      memcpy(buffer, recvBuffer, 64);
      return true;
    }
    bool write(const uint8_t* buffer){
      memcpy(sendBuffer+sendBufferIndex, buffer, 64);
      sendBufferIndex += 64;
      return true;
    }
};

void setup_test();
void run_tests();
void test_single_incoming_frame(void);
void test_multiple_incoming_frame(void);
void test_multiple_unique_incoming_frame(void);
void test_single_outgoing_single_frame(void);
void test_single_outgoing_multiple_frame(void);