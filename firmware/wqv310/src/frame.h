#pragma once
#include <Arduino.h>

#include <span>
#include <string>

using namespace std;

/**
 * The Frame is the common building block of the comms protocol.
 * Every frame contains the bytes: Begin, Address, Control, [Data (n bytes)], Checksum (2 bytes), End.
 * Begin and End byte markers are only allowed at the beginning and end and are "escaped" into two bytes elsewhere.
 */
namespace Frame {

/** Smallest frame with no data */
const size_t FRAME_SIZE = 6;

const uint8_t FRAME_BOF = 0xc0;
const uint8_t FRAME_EOF = 0xc1;
const uint8_t FRAME_ESC = 0x7d;

const unsigned long DEFAULT_READ_TIMEOUT = 1000;

enum ReadError { FRAME_OK = 0, FRAME_TIMEOUT, FRAME_READ_ERROR, FRAME_CRC_FAIL, FRAME_MALFORMED, FRAME_APP_ERROR };

struct Frame {
    ReadError error;
    uint8_t port;
    uint8_t seq;
    std::span<const uint8_t> data;
};

Frame readFrame(unsigned long timeout = DEFAULT_READ_TIMEOUT);

// NOTE when Serial is not connected, the ESP32 can be too fast and the watch misses the beginning,
// so the default is to repeat the C0 many times (WQV Link does the same). May need further tuning.
// Underclocking the ESP32 is also an option.
void writeFrame(uint8_t addr, uint8_t control, span<const uint8_t> data = {}, size_t repeatBOF = 10);

std::string extractString(Frame frame, size_t offset, size_t len);

void log(Frame f);

Frame errorFrame(ReadError err);

}  // namespace Frame