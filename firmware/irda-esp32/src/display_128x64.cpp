#ifdef ENABLE_DISPLAY_128x64
#include <Adafruit_SSD1306.h>

#include "_1980v23P04_16.h"
#include "config.h"
#include "display.h"
#include "log.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

namespace Display {

const char* TAG = "Display";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

static const unsigned char PROGMEM image_frame0_bits[]{0xff, 0xfc, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                                       0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                                       0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0xff, 0xfc};

static const unsigned char PROGMEM image_frame1_bits[]{0xff, 0xfc, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                                       0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                                       0xf0, 0x04, 0x50, 0x04, 0x30, 0x04, 0x1f, 0xfc};

static const unsigned char PROGMEM image_frame2_bits[]{0xff, 0xfc, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                                       0x80, 0x04, 0x80, 0x04, 0xfe, 0x04, 0x42, 0x04, 0x22, 0x04,
                                                       0x12, 0x04, 0x0a, 0x04, 0x06, 0x04, 0x03, 0xfc};

static const unsigned char PROGMEM image_frame3_bits[]{0xff, 0xfc, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                                       0xff, 0x84, 0x40, 0x84, 0x20, 0x84, 0x10, 0x84, 0x08, 0x84,
                                                       0x04, 0x84, 0x02, 0x84, 0x01, 0x84, 0x00, 0xfc};

static const unsigned char PROGMEM image_frame4_bits[]{0xff, 0xfc, 0x80, 0x04, 0x80, 0x04, 0xff, 0xe4, 0x40, 0x24,
                                                       0x20, 0x24, 0x10, 0x24, 0x08, 0x24, 0x04, 0x24, 0x02, 0x24,
                                                       0x01, 0x24, 0x00, 0xa4, 0x00, 0x64, 0x00, 0x3c};

static const unsigned char* const frames[] PROGMEM{image_frame0_bits, image_frame1_bits, image_frame2_bits,
                                                   image_frame3_bits, image_frame4_bits};

static const unsigned char PROGMEM image_arrow_left_bits[]{0x20, 0x40, 0xfe, 0x40, 0x20};

static const unsigned char PROGMEM image_arrow_right_bits[]{0x08, 0x04, 0xfe, 0x04, 0x08};

static const unsigned char PROGMEM image_file_save_bits[]{
    0x7f, 0xfc, 0x90, 0xaa, 0x90, 0xa9, 0x90, 0xe9, 0x90, 0x09, 0x8f, 0xf1, 0x80, 0x01, 0x80, 0x01,
    0x80, 0x01, 0x9f, 0xf9, 0x90, 0x09, 0x97, 0xe9, 0x90, 0x09, 0xd7, 0xeb, 0x90, 0x09, 0x7f, 0xfe};

bool init() {
    if (!Wire.setPins(PIN_I2C_SDA, PIN_I2C_SCL) || !Wire.begin()) {
        LOGE(TAG, "Can't use these pins for I2C: SDA=%d SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);
        return false;
    }
    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        LOGE(TAG, "SSD1306 allocation failed");
        return false;
    }

    display.clearDisplay();
    display.display();

    return true;
}

void showIdleScreen() {
    static const unsigned char PROGMEM image_Layer_12_bits[] = {
        0xff, 0xff, 0xff, 0x80, 0x00, 0x01, 0x8e, 0x00, 0x71, 0x91, 0x00, 0x89, 0xa0, 0x81, 0x05, 0xa0, 0x81,
        0x05, 0xa0, 0x81, 0x05, 0x91, 0x00, 0x89, 0x8e, 0x00, 0x71, 0x80, 0x00, 0x01, 0xff, 0xff, 0xff};

    display.clearDisplay();

    display.fillRect(0, 0, 128, 9, 1);

    display.setTextColor(1);
    display.setTextWrap(false);
    display.setCursor(26, 26);
    display.print("IR > COM > PC");

    display.setCursor(38, 15);
    display.print("On watch:");

    display.setTextColor(0);
    display.setFont(&_1980v23P04_16);
    display.setCursor(93, 7);
    display.print("WQV-1");

    display.setCursor(1, 7);
    display.print("SEARCHING");

    display.setTextColor(1);
    display.setFont();
    display.setCursor(31, 50);
    display.print("Aim at");

    display.drawBitmap(71, 48, image_Layer_12_bits, 24, 11, 1);

    display.display();
}

void showConnectingScreen(int offset) {
    display.clearDisplay();

    display.fillRect(0, 0, 128, 9, 1);

    display.setTextColor(0);
    display.setTextWrap(false);
    display.setFont(&_1980v23P04_16);
    display.setCursor(93, 7);
    display.print("WQV-1");

    display.setCursor(1, 7);
    display.print("CONNECTING");

    display.drawBitmap(48 + offset, 34, image_arrow_right_bits, 7, 5, 1);

    display.drawBitmap(69 - offset, 34, image_arrow_left_bits, 7, 5, 1);

    display.display();
}

void showProgressScreen(size_t bytes, size_t totalBytes, size_t bytesPerImage, const char* step) {
    static uint8_t frame = 0;

    display.clearDisplay();

    // status_downloading
    // display.setCursor(1, 7);
    // display.print("DOWNLOADING");

    // bar_border
    display.drawRect(1, 53, 125, 9, 1);

    // bar_fill
    // display.fillRect(3, 55, 121, 5, 1);

    // percent
    display.setTextColor(1);
    display.setCursor(1, 50);
    display.printf("%0.0f%%", 100.0f * bytes / totalBytes);

    // photo_count
    display.setFont();
    display.setCursor(38, 25);
    display.printf("Photo %d/%d", (int)ceil((float)bytes / bytesPerImage), totalBytes / bytesPerImage);

    // Progress bar, from w=0 to w=121
    display.fillRect(3, 55, bytes * 122 / totalBytes, 5, 1);

    // status_bar
    display.fillRect(0, 0, 128, 9, 1);

    // status_wqv
    display.setTextColor(0);
    display.setTextWrap(false);
    display.setFont(&_1980v23P04_16);
    display.setCursor(93, 7);
    display.print("WQV-1");

    // status_downloading
    display.setCursor(1, 7);
    display.print(step);

    display.drawBitmap(22, 21, frames[(frame++ / 10) % 5], 14, 14, 1);

    display.display();
}

void showMountedScreen() {
    display.clearDisplay();

    // file_save
    display.drawBitmap(56, 19, image_file_save_bits, 16, 16, 1);

    // Layer 5
    display.setFont();
    display.setTextColor(1);
    display.setTextWrap(false);
    display.setCursor(14, 43);
    display.print("USB drive mounted");

    // Layer 5 copy
    display.setCursor(17, 54);
    display.print("Eject when done!");

    // Layer 6
    display.fillRect(0, 0, 128, 9, 1);

    // Layer 9
    display.setTextColor(0);
    display.setFont(&_1980v23P04_16);
    display.setCursor(93, 7);
    display.print("WQV-1");

    // Layer 2 copy
    display.setCursor(1, 7);
    display.print("MOUNTED");

    display.display();
}

// Currently unused
size_t printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    size_t result = display.printf(format, args);
    va_end(args);
    return result;
}

}  // namespace Display

#endif