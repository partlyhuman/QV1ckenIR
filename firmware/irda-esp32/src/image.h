#pragma once
#include <Arduino.h>

#include "FS.h"

namespace Image {

const size_t W = 120;
const size_t H = 120;

struct __attribute__((packed)) Image {
    char name[24];  // space padded, not null-terminated
    uint8_t year_minus_2000;
    uint8_t month;
    uint8_t day;
    uint8_t hour;              // NOTE the spec had these reversed
    uint8_t minute;            // NOTE the spec had these reversed
    uint8_t pixel[W * H / 2];  // one nibble per pixel
};
// If this isn't correct, don't compile
static_assert(sizeof(Image) == 0x1C3D, "Image size mismatch");

bool init();
void exportImagesFromDump(File &dump);

}  // namespace Image
