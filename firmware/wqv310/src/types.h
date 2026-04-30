#pragma once

#include <Arduino.h>

struct __attribute__((packed)) Timestamp {
    uint8_t year2k;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
};
