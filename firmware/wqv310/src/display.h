#pragma once
#include <Arduino.h>

namespace Display {

bool init();
void dim(bool dim);
void showIdleScreen();
void showConnectingScreen(int offset = 0);
void showProgressScreen(size_t chunkNumber, size_t totalChunks, size_t imageNumber, const char* step = "DOWNLOADING");
void showMountedScreen();

}  // namespace Display