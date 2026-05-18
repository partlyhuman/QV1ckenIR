#pragma once

#include <Arduino.h>

#include <string>
#include <vector>

namespace Image {

void init();
void postProcess(std::string fileName, std::vector<uint8_t> data, int wqvModel);

}  // namespace Image