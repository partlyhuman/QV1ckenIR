#ifndef ENABLE_DISPLAY_128x64
#ifndef ENABLE_DISPLAY_RGBLED
#include "display.h"

namespace Display {

bool init() {
    return false;
}
void dim(bool d) {
}
void showIdleScreen() {
}
void showConnectingScreen(int offset) {
}
void showProgressScreen(size_t bytes, size_t totalBytes, size_t bytesPerImage, const char* label) {
}
void showMountedScreen() {
}

}  // namespace Display
#endif
#endif