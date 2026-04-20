#include "crc16.h"

void crc16x25_byte_lsb(uint16_t &crc, uint8_t b) {
    // const uint16_t POLY  = 0b0001000000100001;
    const uint16_t POLY_LSB = 0b1000010000001000;
    crc ^= b;
    for (int bit = 0; bit < 8; bit++) {
        if (crc & 0x0001) {
            crc = (crc >> 1) ^ POLY_LSB;
        } else {
            crc >>= 1;
        }
    }
}

uint16_t crc16x25_buffer(const uint8_t *data, size_t len, uint16_t crc) {
    for (int i = 0; i < len; i++) {
        crc16x25_byte_lsb(crc, data[i]);
    }

    // XOROUT == true
    return crc ^ 0xffff;
}