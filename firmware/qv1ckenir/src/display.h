#pragma once
#include <Arduino.h>

namespace Display {

bool init();
void dim(bool dim);
void showIdleScreen();
void showConnectingScreen(int offset = 0);
void showProgressScreen(size_t bytes, size_t totalBytes, size_t bytesPerImage, const char* label = "DOWNLOADING");
void showMountedScreen();

}  // namespace Display