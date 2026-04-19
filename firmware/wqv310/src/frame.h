#pragma once
#include <Arduino.h>

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

void writeFrame(uint8_t addr, uint8_t control, const uint8_t *data = nullptr, size_t len = 0);
bool parseFrame(uint8_t *buf, size_t len, size_t &outLen, uint8_t &addr, uint8_t &control);

}  // namespace Frame