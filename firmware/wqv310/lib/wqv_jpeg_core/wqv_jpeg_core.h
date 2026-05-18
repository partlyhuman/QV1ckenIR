#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "wqv_types.h"

time_t timestampToTime(const Timestamp src);
std::vector<uint8_t> makeExifBlob(const Timestamp &t, const std::string &title, int wqvModel);
std::pair<std::string, Timestamp> parseCasioJpegMetadata(std::vector<uint8_t> &data, bool replaceWithExif = false,
                                                         int wqvModel = 0);
