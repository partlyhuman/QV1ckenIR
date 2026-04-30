#pragma once

#include <Arduino.h>

#include <span>
#include <string>
#include <utility>

namespace Image {

std::string trimTrailingSpaces(std::string src);

void postProcess(std::string fileName, size_t fileSize);

}  // namespace Image