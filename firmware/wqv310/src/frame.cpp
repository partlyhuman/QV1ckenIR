#include "frame.h"

#include <array>
#include <span>

#include "config.h"
#include "crc16.h"
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

void writeFrame(uint8_t addr, uint8_t control, span<const uint8_t> data, size_t repeatBOF) {
#ifdef DEBUG_WRITE
#if LOG_LEVEL >= 3
    Serial.printf("> %02x %02x  ", addr, control);
    for (auto b : data) Serial.printf("%02x ", b);
    Serial.println();
#endif
#endif
    IRDA_tx(true);

    for (size_t i = 0; i < repeatBOF; i++) {
        IRDA.write(FRAME_BOF);
    }

    uint16_t crc = 0xffff;

    writeEscaped(addr);
    crc16x25_byte_lsb(crc, addr);

    writeEscaped(control);
    crc16x25_byte_lsb(crc, control);

    for (uint8_t b : data) {
        crc16x25_byte_lsb(crc, b);
        writeEscaped(b);
    }

    crc ^= 0xffff;

    writeEscaped(crc & 0xff);
    writeEscaped((crc >> 8) & 0xff);
    // #ifdef DEBUG_WRITE
    // #if LOG_LEVEL >= 3
    //     Serial.printf(" %02x %02x\n", crc & 0xff, crc >> 8);
    // #endif
    // #endif

    IRDA.write(FRAME_EOF);
    IRDA.flush(true);

    IRDA_tx(false);
}

ReadError parseFrame(uint8_t *buf, size_t len, size_t &outLen, uint8_t &addr, uint8_t &control) {
    uint16_t crc = 0xffff;
    size_t i = 0;
    outLen = 0;

    // Allow leading 0xff padding
    for (; buf[i] == 0xff && i < len; i++);

    if (buf[i] != FRAME_BOF) {
        LOGE(TAG, "Frame does not start with BOF");
        return FRAME_READ_ERROR;
    }

    // Allow any number of BOF markers
    for (; buf[i] == FRAME_BOF && i < len; i++);

    size_t index_addr = i;
    if ((len - index_addr) < 3) {
        LOGE(TAG, "Read data shorter than minimum frame length");
        return FRAME_MALFORMED;
    }

    addr = buf[i++];
    control = buf[i++];
    crc16x25_byte_lsb(crc, addr);
    crc16x25_byte_lsb(crc, control);

    // Unescape, overwriting the buffer as it will only get shorter
    // Stop before EOF, we already verified that (this also means we don't have to check length when unescaping)
    for (; i < len; i++) {
        uint8_t b = buf[i];
        // We could have gotten duplicate messages, end if this contains two (TODO this will drop messages...)
        if (b == FRAME_EOF) {
            LOGE(TAG, "Early EOF, readUntilByte should have caught this");
            return FRAME_MALFORMED;
        }
        if (b == FRAME_ESC && i + 1 < len) {
            b = buf[++i] ^ 0x20;
        }
        // Rewrite the stream
        buf[outLen++] = b;
    }

    if (outLen < 2) {
        LOGE(TAG, "Unescaped string too short");
        return FRAME_MALFORMED;
    }

    uint16_t expectedcrc = buf[outLen - 2] | (buf[outLen - 1] << 8);
    // When done the buffer will contain only the data frame, strip off the crc
    outLen -= 2;

    // CRC the remaining data in the unescaped buffer (continuing from the addr+cmd)
    crc = crc16x25_buffer(buf, outLen, crc);

#ifdef DEBUG_READ
#if LOG_LEVEL >= 3
    Serial.printf("< %02x %02x  ", addr, control);
    for (size_t i = 0; i < outLen; i++) {
        Serial.printf("%02x ", buf[i]);
    }
    Serial.println();
#endif
#endif

    if (expectedcrc != crc) {
        LOGE(TAG, "Expected crc %04x, calculated %04x", expectedcrc, crc);
        return FRAME_CRC_FAIL;
    }

    return FRAME_OK;
}

// TODO we should have a higher-level thing that replies with a state 0x53 when we parse fail or resends last when
// timing out
Frame readFrame(unsigned long timeout) {
    // Packets seem to be up to 300 bytes
    const size_t BUFFER_SIZE{1024};
    static uint8_t readBuffer[BUFFER_SIZE];

    Frame result{};

    uint8_t seq;
    uint8_t port;
    size_t len = 0;
    size_t dataLen = 0;

    IRDA.setTimeout(timeout);
    len = IRDA.readBytesUntil(FRAME_EOF, readBuffer, BUFFER_SIZE);
    if (len == 0) {
        LOGW(TAG, "Timeout...");
        result.error = FRAME_TIMEOUT;
        return result;
    }
    if (len == BUFFER_SIZE) {
        LOGE(TAG, "Filled buffer up all the way, probably dropping content");
        result.error = FRAME_READ_ERROR;
        return result;
    }

    result.error = parseFrame(readBuffer, len, dataLen, port, seq);
    if (result.error == FRAME_OK) {
        result.port = port;
        result.seq = seq;
        result.data = std::span(readBuffer, dataLen);
    }
    return result;
}

std::string extractString(Frame frame, size_t offset, size_t len) {
    if (offset + len > frame.data.size()) {
        LOGE(TAG, "Asked for string out of range of data");
        return "";
    }

    auto charPtr = reinterpret_cast<const char *>(frame.data.data());
    return std::string(charPtr + offset, len);
}

};  // namespace Frame
