#ifdef ENABLE_DISPLAY_72x40
#include <Arduino.h>

#include "U8g2lib.h"
#include "display.h"

U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE, 6, 5);

namespace Display {

bool init() {
    u8g2.begin();
    return true;
}

void showIdleScreen() {
    u8g2.clearBuffer();
    u8g2.setFontMode(1);
    u8g2.setBitmapMode(1);

    // count
    u8g2.setFont(u8g2_font_spleen32x64_mf);
    u8g2.setCursor(0, 40);
    u8g2.print(100);

    u8g2.sendBuffer();
}

void showConnectingScreen(int offset) {
}

void showProgressScreen(size_t bytes, size_t totalBytes, size_t bytesPerImage, const char* label) {
}

void showMountedScreen() {
}

void end() {
    u8g2.clearDisplay();
    u8g2.clearBuffer();
    u8g2.sleepOn();
}

}  // namespace Display
#endif