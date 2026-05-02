#pragma once

#include <Arduino.h>

namespace Firmware
{
	void init();
	void rebootIntoPartition(int part);
}