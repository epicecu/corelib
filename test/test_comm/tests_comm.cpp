#include "tests_comm.h"
#include "transaction.pb.h"

uint8_t sendBuffer[192] = {0};
uint8_t sendBufferIndex = 0;
uint8_t recvBuffer[64] = {0};

// External interfaces
FastCRC32 CRC32;

// Class under test
TestComm com;

void setup_test()
{
  com.testReset();
}

void run_tests()
{
  UNITY_BEGIN(); // IMPORTANT LINE!
  RUN_TEST(test_single_incoming_frame);
  RUN_TEST(test_multiple_incoming_frame);
  RUN_TEST(test_multiple_unique_incoming_frame);
  RUN_TEST(test_single_outgoing_single_frame);
  RUN_TEST(test_single_outgoing_multiple_frame);
  RUN_TEST(test_message_callback);
  UNITY_END(); // stop unit testing
}

void test_single_incoming_frame(void)
{
  setup_test();
  
  corelib::Frame frame1;
  frame1.preamble = corelib::Preamble::DATA;
  frame1.destinationAddress = 0x00;
  frame1.sourceAddress = 0x01; // pretend to be PC
  frame1.frameTotal = 1;
  frame1.frameOrder = 1;
  frame1.frameID = 0xDEADBEEF;
  memset(frame1.payload, 0x7F, sizeof(corelib::Frame::payload));
  frame1.crc = CRC32.crc32((uint8_t*)&frame1, sizeof(corelib::Frame)-4);

  com.initilise();

  memcpy(recvBuffer, (uint8_t*)&frame1, sizeof(corelib::Frame));
  com.testProcessRead();
  com.testProcessIncomingMessage();
  auto frames = com.getInFrames();
  TEST_ASSERT_EQUAL(0, frames.size());

  auto buffer = com.getBuffer();
  TEST_ASSERT_EQUAL(sizeof(corelib::Frame::payload), buffer.inIndex);
}

void test_multiple_incoming_frame(void)
{
  setup_test();

  corelib::Frame frame1;
  frame1.preamble = corelib::Preamble::DATA;
  frame1.destinationAddress = 0x00;
  frame1.sourceAddress = 0x01; // pretend to be PC
  frame1.frameTotal = 2;
  frame1.frameOrder = 1;
  frame1.frameID = 0xDEADBEEF;
  memset(frame1.payload, 0x7F, sizeof(corelib::Frame::payload));
  frame1.crc = CRC32.crc32((uint8_t*)&frame1, sizeof(corelib::Frame)-4);

  corelib::Frame frame2;
  frame2.preamble = corelib::Preamble::DATA;
  frame2.destinationAddress = 0x00;
  frame2.sourceAddress = 0x01; // pretend to be PC
  frame2.frameTotal = 2;
  frame2.frameOrder = 2;
  frame2.frameID = 0xDEADBEEF;
  memset(frame2.payload, 0x80, sizeof(corelib::Frame::payload));
  frame2.crc = CRC32.crc32((uint8_t*)&frame2, sizeof(corelib::Frame)-4);

  com.initilise();

  memcpy(recvBuffer, (uint8_t*)&frame1, sizeof(corelib::Frame));
  com.testProcessRead();
  com.testProcessIncomingMessage();
  auto frames1 = com.getInFrames();
  TEST_ASSERT_EQUAL(1, frames1.size()); // should only be 1 unique id
  auto first1 = frames1.begin();
  TEST_ASSERT_EQUAL(0xDEADBEEF,first1->first); // id should match
  auto first1Frames = first1->second;
  TEST_ASSERT_EQUAL(1, first1Frames.size()); // only 1 frame for the id at this time

  auto buffer1 = com.getBuffer();
  TEST_ASSERT_EQUAL(0, buffer1.inIndex);

  memcpy(recvBuffer, (uint8_t*)&frame2, sizeof(corelib::Frame));
  com.testProcessRead();
  com.testProcessIncomingMessage();
  auto frames2 = com.getInFrames();
  TEST_ASSERT_EQUAL(0, frames2.size()); // should only be 0 since we had enough frames to process successfully

  auto buffer2 = com.getBuffer();
  TEST_ASSERT_EQUAL(100, buffer2.inIndex);
  for(uint8_t i = 0; i < sizeof(corelib::Frame::payload); i++){
    TEST_ASSERT_EQUAL(0x7F, buffer2.inBuffer[i]);
  }
  for(uint8_t i = sizeof(corelib::Frame::payload); i < sizeof(corelib::Frame::payload)*2; i++){
    TEST_ASSERT_EQUAL(0x80, buffer2.inBuffer[i]);
  }
}

void test_multiple_unique_incoming_frame(void)
{
  setup_test();

  corelib::Frame frame1;
  frame1.preamble = corelib::Preamble::DATA;
  frame1.destinationAddress = 0x00;
  frame1.sourceAddress = 0x01; // pretend to be PC
  frame1.frameTotal = 2;
  frame1.frameOrder = 1;
  frame1.frameID = 0xDEADBEE0;
  memset(frame1.payload, 0x7F, sizeof(corelib::Frame::payload));
  frame1.crc = CRC32.crc32((uint8_t*)&frame1, sizeof(corelib::Frame)-4);

  corelib::Frame frame2;
  frame2.preamble = corelib::Preamble::DATA;
  frame2.destinationAddress = 0x00;
  frame2.sourceAddress = 0x01; // pretend to be PC
  frame2.frameTotal = 2;
  frame2.frameOrder = 2;
  frame2.frameID = 0xDEADBEE1;
  memset(frame2.payload, 0x80, sizeof(corelib::Frame::payload));
  frame2.crc = CRC32.crc32((uint8_t*)&frame2, sizeof(corelib::Frame)-4);

  com.initilise();

  memcpy(recvBuffer, (uint8_t*)&frame1, sizeof(corelib::Frame));
  com.testProcessRead();
  com.testProcessIncomingMessage();
  auto frames1 = com.getInFrames();
  TEST_ASSERT_EQUAL(1, frames1.size()); // should only be 1 unique id

  memcpy(recvBuffer, (uint8_t*)&frame2, sizeof(corelib::Frame));
  com.testProcessRead();
  com.testProcessIncomingMessage();
  auto frames2 = com.getInFrames();
  TEST_ASSERT_EQUAL(2, frames2.size()); // should only be 0 since we had enough frames to process successfully
}

void test_single_outgoing_single_frame(void)
{
  setup_test();

  corelib::Buffer buffer;
  memset(buffer.outBuffer, 0x7F, sizeof(corelib::Frame::payload));
  buffer.outMessageLength = sizeof(corelib::Frame::payload);

  com.setBuffer(buffer);
  com.testProcessOutgoingMessage();
  com.testProcessWrite();

  corelib::Frame frame;
  memcpy(&frame, sendBuffer, sizeof(corelib::Frame));

  for(uint8_t i = 0; i < sizeof(corelib::Frame::payload); i++){
    TEST_ASSERT_EQUAL(0x7F, frame.payload[i]);
  }
}

void test_single_outgoing_multiple_frame(void)
{
  setup_test();

  corelib::Buffer buffer;
  memset(buffer.outBuffer, 0x7F, TransactionMessage_size);
  buffer.outMessageLength = TransactionMessage_size;

  com.setBuffer(buffer);
  com.testProcessOutgoingMessage();
  com.testProcessWrite();

  corelib::Frame frame1;
  memcpy(&frame1, sendBuffer, sizeof(corelib::Frame));

  corelib::Frame frame2;
  memcpy(&frame2, sendBuffer+(sizeof(corelib::Frame)*1), sizeof(corelib::Frame));


  for(uint8_t i = 0; i < sizeof(corelib::Frame::payload); i++){
    TEST_ASSERT_EQUAL(0x7F, frame1.payload[i]);
  }

  for(uint8_t i = 0; i < TransactionMessage_size % sizeof(corelib::Frame::payload); i++){
    TEST_ASSERT_EQUAL(0x7F, frame2.payload[i]);
  }
}

void test_message_callback(void)
{
  setup_test();

  corelib::Buffer buffer;

  // Test data
  memset(buffer.inBuffer, 0x7F, sizeof(corelib::Frame::payload));
  buffer.inMessageLength = sizeof(corelib::Frame::payload);

  // test the request message
  auto fn = [](corelib::Buffer* i){ 
    TEST_ASSERT_EQUAL(sizeof(corelib::Frame::payload), i->inMessageLength);
    i->outMessageLength = 0x99; // Check that this value has changed
    return corelib::HandleMessageState::OK;
  };

  com.setHandleMessageCallback(fn);

  // Test that the callback function is called
  com.testCallback(&buffer);

  // Check value has changed from within the lamda function
  TEST_ASSERT_EQUAL(0x99, buffer.outMessageLength);
}

void setUp (void) {}

void tearDown (void) {}

int main(int argc, char **argv) {
  run_tests();
  return 0;
}