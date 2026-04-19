#pragma once

namespace MassStorage {

extern bool media_present;

void init();
void shutdown();
void begin();
void end();
void toggle();

}  // namespace MassStorage