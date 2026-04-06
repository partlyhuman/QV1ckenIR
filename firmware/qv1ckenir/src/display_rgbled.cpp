#ifdef ENABLE_DISPLAY_RGBLED
#include <Adafruit_NeoPixel.h>

#include "display.h"

namespace Display {

Adafruit_NeoPixel pixel(1, PIN_WS2812, NEO_GRB);

bool init() {
    bool ok = pixel.begin();
    if (ok) {
        pixel.setBrightness(32);
        pixel.clear();
        pixel.show();
    }
    return ok;
}

void dim(bool d) {
}

void showIdleScreen() {
    pixel.clear();
    pixel.show();
}

void showConnectingScreen(int offset) {
    pixel.clear();
    pixel.show();
    delay(100);
    pixel.setPixelColor(0, pixel.Color(0, 255, 0));
    pixel.show();
}

void showProgressScreen(size_t bytes, size_t totalBytes, size_t bytesPerImage, const char* label) {
    pixel.clear();
    pixel.setPixelColor(0, pixel.ColorHSV(20000 * bytes / totalBytes, 255, 255));
    pixel.show();
}

void showMountedScreen() {
    pixel.clear();
    pixel.setPixelColor(0, pixel.Color(0, 0, 255));
    pixel.show();
}

}  // namespace Display

#endif