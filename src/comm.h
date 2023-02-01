#ifndef COMMS_H
#define COMMS_H

#include <Arduino.h>
#include "function.h"
#include "frame.h"

#include <pb_decode.h>
#include <pb_encode.h>
#include <FastCRC.h>
#include <etl/map.h>
#include <etl/vector.h>
#include <etl/delegate.h>

#define NUMBER_FRAME_SETS 3

namespace corelib {

struct Buffer {
    // In data
    uint8_t inBuffer[128] = {0};
    int inIndex = 0;
    int inMessageLength = 0;
    // Out data
    uint8_t outBuffer[128] = {0};
    int outMessageLength = 0;
};

enum class HandleMessageState : uint8_t {
    OK = 0,
    ERROR = 1,
    NO_DATA = 2,
    FAILED_DECODE = 3,
    FAILED_ENCODE = 4
};

enum class ReadState : uint8_t {
    OK = 0,
    ERROR = 1,
    IN_FRAMES_FULL = 2,
    NO_DATA = 3,
    MISMATCH_CRC = 4
};

enum class WriteState : uint8_t {
    OK = 0,
    ERROR = 1,
    OUT_FRAMES_EMPTY = 2
};

enum class ProcessState : uint8_t {
    OK = 0,
    ERROR = 1
};

/**
 * @brief The Comms class provides a base set of communication functions to interface with
 * other hardware and the PC.
 * Comms is a base class.
 */
class Comm : public Function
{
public:
    /**
     * @brief Construct a new Comms object
     * 
     */
    Comm(): CRC32(), inFrames(), outFrames() {
        initilised = false;
    }

    /**
     * @brief Register a callback function which will be invoked after incomeing 
     * data is ready for processing.
     */
    void setHandleMessageCallback(etl::delegate<HandleMessageState(Buffer*)> fn) {
        callbackFunction = fn;
    }

protected:
    // @brief Incoming/Outgoing Message Buffer
    Buffer buffer;
    // @brief Set when the Comms Interface is initilised
    bool initilised;
    // @brief The CRC Interface
    FastCRC32 CRC32;
    // @brief This Device's Address
    uint8_t address = 0x02;
    // @brief  The destination address to communicate with
    uint8_t destinationDeviceAddress = 0x01; // Sending to PC

    // @brief  List of in Frames by FrameId
    etl::map<uint32_t, etl::vector<Frame, 3>, NUMBER_FRAME_SETS> inFrames;
    // @brief  List of out Frames by FrameId
    etl::map<uint32_t, etl::vector<Frame, 3>, NUMBER_FRAME_SETS> outFrames;
    
    // Callback function
    etl::delegate<HandleMessageState(Buffer*)> callbackFunction;

    /**
     * @brief A callback function which Handles the incoming message, and 
     * may provide an outgoing message.  
     * 
     * @param buffer 
     * @return HandleMessageState 
     */
    HandleMessageState callback(Buffer* buffer) {
        return callbackFunction(buffer);
    }

    /**
     * @brief Processes the incoming message from the using proto function
     * to a set of Frames
     */
    ProcessState processIncomingMessage() {
        for(auto uniquePair : inFrames){
            auto frames = uniquePair.second;
            auto frame = frames.front();
            // Reset in buffer
            buffer.inIndex=0;
            // Single frame message
            if(frame.frameTotal == 1){
                memcpy(buffer.inBuffer, frame.payload, sizeof(frame.payload));
                buffer.inIndex=sizeof(frame.payload);
                // Remove this frameId from the map
                inFrames.erase(uniquePair.first);
                // Multiple frame message
            }else{
                // Check if we have the remaining data..
                if(frames.size() == frame.frameTotal){
                    for (uint8_t order = 1; order <= frame.frameTotal; order++){
                        // Search for a frame with order
                        auto frame = etl::find_if(frames.begin(), frames.end(), [order](Frame frame){
                            return frame.frameOrder == order;
                        });
                        // A frame has been found
                        if(frame != frames.end()){
                            const uint32_t frameSize = sizeof(frame->payload);
                            memcpy(buffer.inBuffer+buffer.inIndex, frame->payload, frameSize);
                            buffer.inIndex+=frameSize;
                        }else{
                            // Something went wrong...
                            // Cancel out of this search
                            buffer.inIndex = 0;
                        }
                    }
                    // Remove this frameId from the map
                    inFrames.erase(uniquePair.first);
                    break; // Exit for loop & Process this message
                }
            }
        }
        return ProcessState::OK;
    }

    /**
     * @brief Processes the outgoing message from the using proto function
     * to a set of Frames
     */
    ProcessState processOutgoingMessage() {
        // Transmit output buffer
        if(buffer.outMessageLength == 0){
            // The outgoing buffer is empty
            return ProcessState::ERROR;
        }
        if(outFrames.full()){
            // Cannot process the outgoing buffer since the outFrames are full
            return ProcessState::ERROR;
        }
        // New frame
        etl::vector<Frame, 3> newFrames;
        // determine how many frames are required for the output message.
        uint8_t requiredFrames = ceil((double)buffer.outMessageLength/sizeof(Frame::payload));
        if(requiredFrames > newFrames.max_size()){
            return ProcessState::ERROR;
        }
        // share id between frames for the message
        uint32_t frameId = random();
        for(uint8_t i = 0; i < requiredFrames; i++){
            // Send out the message over `requiredFrames` times
            Frame frame;
            frame.preamble = Preamble::DATA;
            frame.sourceAddress = address;
            frame.destinationAddress = destinationDeviceAddress;
            frame.frameTotal = requiredFrames;
            frame.frameOrder = i+1;
            frame.frameID = frameId;
            memcpy(frame.payload, buffer.outBuffer+(i*sizeof(Frame::payload)), sizeof(Frame::payload));
            // Check that the frame arrived correctly
            frame.crc = CRC32.crc32((uint8_t*)&frame, sizeof(Frame)-4);
            newFrames.push_back(frame);
        }
        // Clear
        buffer.outMessageLength = 0;
        // Save frames to the outFrames
        outFrames.insert(etl::pair<uint32_t, etl::vector<Frame, 3>>{frameId, newFrames});
        return ProcessState::OK;
    }

    /**
     * @brief Processes any incoming frames
     * 
     * @return ReadState 
     */
    ReadState processRead() {
        uint8_t incoming[64] = {0}; // incoming frame in bytes

        if(inFrames.full()){
            return ReadState::IN_FRAMES_FULL;
        }

        if(read(incoming) == 0){
            return ReadState::NO_DATA;
        }

        // Frame arrived
        Frame frame;
        memcpy(&frame, incoming, 64);

        // Check that the frame arrived correctly
        uint32_t crc = CRC32.crc32((uint8_t*)&frame, sizeof(Frame)-4);

        // Frame integrity safe
        if(crc != frame.crc){
            return ReadState::MISMATCH_CRC;
        }

        // Data request
        if(frame.preamble == Preamble::DATA){
            // Check if the frame is for this device
            if(frame.destinationAddress == address || frame.destinationAddress == 0x00){
            // Add frame to device frame list

            auto it = inFrames.find(frame.frameID);

            if(it != inFrames.end()){
                // Found existing frameId
                it->second.push_back(frame);
            }else{
                // New frame
                etl::vector<Frame, 3> newFrames;
                newFrames.push_back(frame);
                inFrames.insert(etl::pair<uint32_t, etl::vector<Frame, 3>>{frame.frameID, newFrames});
            }

            }else{
            // Relay message
            // todo: Implement this in a new feature
            }
        }else if(frame.preamble == Preamble::PROGRAMMOR_COMPATIBLE_REQUEST){
            // Save frames to the outFrames
            uint32_t frameId = random();
            Frame frameResponse = frame; // copy over frame
            frameResponse.preamble = Preamble::PROGRAMMOR_COMPATIBLE_RESPONSE;
            frameResponse.sourceAddress = frame.destinationAddress;
            frameResponse.destinationAddress = address;
            frameResponse.frameTotal = 1;
            frameResponse.frameOrder = 1;
            frameResponse.frameID = frameId;
            frameResponse.crc = CRC32.crc32((uint8_t*)&frameResponse, sizeof(Frame)-4);
            etl::vector<Frame, 3> newFrames;
            newFrames.push_back(frameResponse);
            outFrames.insert(etl::pair<uint32_t, etl::vector<Frame, 3>>{frameId, newFrames});
        }else if(frame.preamble == Preamble::ARP_REQUEST){
            // ARP request
            // todo: Implement this in a new feature    
        }
        return ReadState::OK;
    }

    /**
     * @brief Processes any outgoing frames
     * 
     * @return WriteState 
     */
    WriteState processWrite() {
        if(outFrames.empty()){
            return WriteState::OUT_FRAMES_EMPTY;
        }
        auto frameSetIt = outFrames.begin();
        auto frames = frameSetIt->second;
        auto it_frames = frames.begin();
        while(it_frames != frames.end()){
            Frame frame = *it_frames;
            if(!write((uint8_t*)&frame)){
            return WriteState::ERROR;
            }
            frames.erase(it_frames);
            if(frames.empty()){
            outFrames.erase(frameSetIt);
            }
        }
        return WriteState::OK;
    }

    /**
     * @brief Physical Read Interface
     * 
     * @param buffer Frame data (64 bytes)
     * @return true 
     * @return false 
     */
    virtual bool read(uint8_t* buffer) = 0;

    /**
     * @brief Physical Write Interface
     * 
     * @param buffer Frame data (64 bytes)
     * @return true 
     * @return false 
     */
    virtual bool write(const uint8_t* buffer) = 0;

    // Function.h
    void performIterate() {
        if(!initilised) {
            return;
        }
        // Read an individual frame
        (void) processRead();
        // Process a set of frames as a message
        (void) processIncomingMessage();
        // Handle message
        (void) callback(&buffer);
        // Process a message as a set of frames
        (void) processOutgoingMessage();
        // Write out individual frames
        (void) processWrite();
    }

};
} // NAMESPACE
#endif // COMMS_H
