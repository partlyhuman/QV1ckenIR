#pragma once
#include <Arduino.h>

namespace Display {

bool init();
void setModel(int m);
void dim(bool dim);
void showBootScreen();
void showIdleScreen();
void showConnectingScreen(int offset = 0);
void showProgressScreen(size_t chunkNumber, size_t totalChunks, size_t imageNumber);
void showMountedScreen();

}  // namespace Display