#pragma once

#include <Arduino.h>

#include <span>
#include <string>
#include <utility>

namespace Meta {

struct __attribute__((packed)) Timestamp {
    uint8_t year2k;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
};

std::string trimTrailingSpaces(std::string src);

void postProcess(std::string fileName);

}  // namespace Meta