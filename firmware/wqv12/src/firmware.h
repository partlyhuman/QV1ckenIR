#pragma once

#include <Arduino.h>

namespace Firmware
{
void init();
void rebootIntoNextPartition();
void rebootIntoPartition(uint part);
}