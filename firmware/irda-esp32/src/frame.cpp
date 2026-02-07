#include "frame.h"

#include "config.h"
#include "irda_hal.h"
#include "log.h"

#define DEBUG_WRITE
#define DEBUG_READ

namespace Frame {

static const char *TAG = "Frame";

static inline void writeEscaped(uint8_t b) {
    if (b == FRAME_BOF || b == FRAME_EOF || b == FRAME_ESC) {
        IRDA.write(FRAME_ESC);
        IRDA.write(b ^ 0x20);
    } else {
        IRDA.write(b);
    }
}

void writeFrame(uint8_t addr, uint8_t control, const uint8_t *data, size_t len) {
#ifdef DEBUG_WRITE
#if LOG_LEVEL >= 3
    Serial.printf("> %02x %02x  ", addr, control);
    for (size_t i = 0; i < len; i++) Serial.printf("%02x ", data[i]);
    Serial.println("");
#endif
#endif
    IRDA_tx(true);

    IRDA.write(FRAME_BOF);

    writeEscaped(addr);
    uint16_t checksum = 0;
    checksum += addr;

    writeEscaped(control);
    checksum += control;

    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        checksum += b;
        writeEscaped(b);
    }

    writeEscaped((checksum >> 8) & 0xff);
    writeEscaped(checksum & 0xff);

    IRDA.write(FRAME_EOF);
    IRDA.flush(true);

    IRDA_tx(false);
}

bool parseFrame(uint8_t *buf, size_t len, size_t &outLen, uint8_t &addr, uint8_t &control) {
    uint16_t checksum = 0;
    uint16_t expectedChecksum;
    outLen = 0;

    if (len < FRAME_SIZE - 1) {
        LOGE(TAG, "Read data shorter than minimum frame length");
        return false;
    }
    if (buf[0] != FRAME_BOF) {
        LOGE(TAG, "Frame does not start with BOF");
        return false;
    }

    addr = buf[1];
    control = buf[2];
    checksum += addr;
    checksum += control;

    // Unescape, overwriting the buffer as it will only get shorter
    // Stop before EOF, we already verified that (this also means we don't have to check length when unescaping)
    for (size_t i = 3; i < len; i++) {
        uint8_t b = buf[i];
        // We could have gotten duplicate messages, end if this contains two (TODO this will drop messages...)
        if (b == FRAME_EOF) {
            LOGE(TAG, "Early EOF, readUntilByte should have caught this");
            return false;
        }
        if (b == FRAME_ESC && i + 1 < len) {
            b = buf[++i] ^ 0x20;
        }
        // Rewrite the stream
        buf[outLen++] = b;
    }

    if (outLen < 2) {
        LOGE(TAG, "Unescaped string too short");
        return false;
    }

    expectedChecksum = (buf[outLen - 2] << 8) | (buf[outLen - 1]);
    // When done the buffer will contain only the data frame, strip off the checksum
    outLen -= 2;

    for (size_t i = 0; i < outLen; i++) {
        checksum += buf[i];
    }

#ifdef DEBUG_READ
#if LOG_LEVEL >= 3
    Serial.printf("< %02x %02x  ", addr, control);
    for (size_t i = 0; i < outLen; i++) {
        Serial.printf("%02x ", buf[i]);
    }
    Serial.println();
#endif
#endif

    if (expectedChecksum != checksum) {
        LOGE(TAG, "Expected checksum %04x, calculated %04x", expectedChecksum, checksum);
        return false;
    }

    return true;
}

};  // namespace Frame