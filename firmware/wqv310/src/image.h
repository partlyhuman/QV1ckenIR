#pragma once

#include <Arduino.h>

#include <span>
#include <string>
#include <utility>

namespace Image {

void init();
void postProcess(std::string fileName, size_t fileSize);

}  // namespace Image