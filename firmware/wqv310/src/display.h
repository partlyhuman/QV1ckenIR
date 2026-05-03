#pragma once
#include <Arduino.h>

namespace Display {

bool init();
void setModel(int m);
void dim(bool dim);
void showBootScreen();
void showIdleScreen();
void showConnectingScreen(const char* text);
void showProgressScreen(float progress, size_t imageNumber);
void showMountedScreen();

}  // namespace Display