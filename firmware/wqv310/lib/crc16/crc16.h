#pragma once

#include <stddef.h>
#include <stdint.h>

void crc16x25_byte_lsb(uint16_t &crc, uint8_t b);
uint16_t crc16x25_buffer(const uint8_t *data, size_t len, uint16_t init = 0xffff);
