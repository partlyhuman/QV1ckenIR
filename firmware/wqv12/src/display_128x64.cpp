#ifdef ENABLE_DISPLAY_128x64
#include <Adafruit_SSD1306.h>

#include "Nokia_Cellphone_FC_8.h"
#include "_1980v23P04_16.h"
#include "config.h"
#include "display.h"
#include "log.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define DIM_AFTER_MS 15000

namespace Display {

static const char* TAG = "Display";
static int screen = -1;

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET, I2C_CLOCK, I2C_CLOCK);

static const unsigned char image_frame0_bits[]{0xff, 0xfc, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                               0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                               0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0xff, 0xfc};

static const unsigned char image_frame1_bits[]{0xff, 0xfc, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                               0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                               0xf0, 0x04, 0x50, 0x04, 0x30, 0x04, 0x1f, 0xfc};

static const unsigned char image_frame2_bits[]{0xff, 0xfc, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                               0x80, 0x04, 0x80, 0x04, 0xfe, 0x04, 0x42, 0x04, 0x22, 0x04,
                                               0x12, 0x04, 0x0a, 0x04, 0x06, 0x04, 0x03, 0xfc};

static const unsigned char image_frame3_bits[]{0xff, 0xfc, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04, 0x80, 0x04,
                                               0xff, 0x84, 0x40, 0x84, 0x20, 0x84, 0x10, 0x84, 0x08, 0x84,
                                               0x04, 0x84, 0x02, 0x84, 0x01, 0x84, 0x00, 0xfc};

static const unsigned char image_frame4_bits[]{0xff, 0xfc, 0x80, 0x04, 0x80, 0x04, 0xff, 0xe4, 0x40, 0x24,
                                               0x20, 0x24, 0x10, 0x24, 0x08, 0x24, 0x04, 0x24, 0x02, 0x24,
                                               0x01, 0x24, 0x00, 0xa4, 0x00, 0x64, 0x00, 0x3c};

static const unsigned char* const frames[] PROGMEM{image_frame0_bits, image_frame1_bits, image_frame2_bits,
                                                   image_frame3_bits, image_frame4_bits};

static const unsigned char image_arrow_left_bits[]{0x20, 0x40, 0xfe, 0x40, 0x20};

static const unsigned char image_arrow_right_bits[]{0x08, 0x04, 0xfe, 0x04, 0x08};

static const unsigned char image_file_save_bits[]{0x7f, 0xfc, 0x90, 0xaa, 0x90, 0xa9, 0x90, 0xe9, 0x90, 0x09, 0x8f,
                                                  0xf1, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x9f, 0xf9, 0x90, 0x09,
                                                  0x97, 0xe9, 0x90, 0x09, 0xd7, 0xeb, 0x90, 0x09, 0x7f, 0xfe};

bool init() {
    LOGV(TAG, "Using these pins for I2C: SDA=%d SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);
    if (!Wire.setPins(PIN_I2C_SDA, PIN_I2C_SCL) || !Wire.begin()) {
        LOGE(TAG, "Can't use these pins for I2C: SDA=%d SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);
        return false;
    }
    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        LOGE(TAG, "SSD1306 allocation failed");
        return false;
    }

#ifdef SCREEN_ROTATION
    display.setRotation(SCREEN_ROTATION);
#endif
    display.clearDisplay();
    display.display();
    dim(false);
    screen = -1;

    LOGV(TAG, "Display init");

    return true;
}

void dim(bool d) {
    if (d) {
        display.ssd1306_command(SSD1306_SETCONTRAST);
        display.ssd1306_command(0x01);
    } else {
        display.ssd1306_command(SSD1306_SETCONTRAST);
        display.ssd1306_command(0xA0);
    }
}

void scrollStatus() {
    // Scrolls the bottom 2 "pages" of 8px
    // Corresponding with the yellow area of the bicolour OLED screens
    // Would be nice to find a way to mount the board rightside up and restore the status bar to the top
    // But at this point that would make firmware updates a bit more difficult for users who already have a rotated
    // screen
    display.ssd1306_command(SSD1306_RIGHT_HORIZONTAL_SCROLL);
    display.ssd1306_command(0x00);
    display.ssd1306_command(0);     // start page
    display.ssd1306_command(0x00);  // interval
    display.ssd1306_command(1);     // end page
    display.ssd1306_command(0x00);
    display.ssd1306_command(0xFF);
    display.ssd1306_command(SSD1306_ACTIVATE_SCROLL);
}

void showIdleScreen() {
    static uint32_t firstIdleScreenTime;

    if (screen == 0) {
        if (millis() - firstIdleScreenTime > DIM_AFTER_MS) {
            dim(true);
        }
        return;
    }
    dim(false);
    firstIdleScreenTime = millis();
    screen = 0;

    static const unsigned char image_Layer_12_bits[] = {
        0xff, 0xff, 0xff, 0x80, 0x00, 0x01, 0x8e, 0x00, 0x71, 0x91, 0x00, 0x89, 0xa0, 0x81, 0x05, 0xa0, 0x81,
        0x05, 0xa0, 0x81, 0x05, 0x91, 0x00, 0x89, 0x8e, 0x00, 0x71, 0x80, 0x00, 0x01, 0xff, 0xff, 0xff};

    display.ssd1306_command(SSD1306_DEACTIVATE_SCROLL);

    display.clearDisplay();

    // Layer 2
    display.setTextColor(1);
    display.setTextWrap(false);
    display.setFont(&Nokia_Cellphone_FC_8);
    display.setCursor(32, 22);
    display.print("IR > COM > PC");

    // Layer 3
    display.setCursor(40, 11);
    display.print("On watch:");

    // Layer 10
    display.fillRect(0, 55, 128, 9, 1);

    // Layer 9
    display.setTextColor(0);
    display.setFont(&_1980v23P04_16);
    // display.setCursor(93, 62);
    // display.print("WQV-1");

    // Layer 2 copy
    display.setCursor(1, 62);
    display.print("SEARCHING");

    // Layer 3 copy
    display.setTextColor(1);
    display.setFont(&Nokia_Cellphone_FC_8);
    display.setCursor(32, 43);
    display.print("Aim at");

    // Layer 12
    display.drawBitmap(71, 35, image_Layer_12_bits, 24, 11, 1);

    display.display();
    scrollStatus();
}

void showConnectingScreen(int offset) {
    screen = 1;

    display.ssd1306_command(SSD1306_DEACTIVATE_SCROLL);
    display.clearDisplay();

    display.drawBitmap(48 + offset, 34, image_arrow_right_bits, 7, 5, 1);

    display.drawBitmap(69 - offset, 34, image_arrow_left_bits, 7, 5, 1);

    // Layer 10
    display.fillRect(0, 55, 128, 9, 1);

    display.setTextColor(0);
    display.setFont(&_1980v23P04_16);
    display.setCursor(93, 62);
    display.print("WQV-1");

    display.setCursor(1, 62);
    display.print("CONNECTING");

    display.display();
}

void showProgressScreen(size_t bytes, size_t totalBytes, size_t bytesPerImage, const char* step) {
    screen = 2;
    static uint8_t frame = 0;

    display.ssd1306_command(SSD1306_DEACTIVATE_SCROLL);
    display.clearDisplay();

    // status_bar
    display.fillRect(0, 55, 128, 9, 1);

    // status_wqv
    display.setTextColor(0);
    display.setTextWrap(false);
    display.setFont(&_1980v23P04_16);
    display.setCursor(93, 62);
    display.print("WQV-1");

    // status_downloading
    display.setCursor(1, 62);
    display.print(step);

    // bar_border
    display.drawRect(1, 38, 125, 9, 1);

    // bar_fill
    display.fillRect(3, 40, bytes * 122 / totalBytes, 5, 1);

    // percent
    display.setTextColor(1);
    display.setCursor(1, 35);
    display.printf("%0.0f%%", 100.0f * bytes / totalBytes);

    // photo_count
    display.setFont(&Nokia_Cellphone_FC_8);
    display.setCursor(46, 14);
    display.printf("Photo %d/%d", (int)ceil((float)bytes / bytesPerImage), totalBytes / bytesPerImage);

    // frame0
    display.drawBitmap(23, 4, frames[(frame++ / 10) % 5], 14, 14, 1);

    display.display();
}

void showMountedScreen() {
    if (screen == 3) return;
    screen = 3;

    display.ssd1306_command(SSD1306_DEACTIVATE_SCROLL);
    display.clearDisplay();

    // file_save
    display.drawBitmap(56, 3, image_file_save_bits, 16, 16, 1);

    // Layer 5
    display.setTextColor(1);
    display.setTextWrap(false);
    display.setFont(&Nokia_Cellphone_FC_8);
    display.setCursor(18, 31);
    display.print("USB drive mounted");

    // Layer 5 copy
    display.setCursor(22, 42);
    display.print("Eject when done!");

    // Layer 6
    display.fillRect(0, 55, 128, 9, 1);

    // Layer 9
    display.setTextColor(0);
    display.setFont(&_1980v23P04_16);
    // display.setCursor(93, 62);
    // display.print("WQV-1");

    // Layer 2 copy
    display.setCursor(1, 62);
    display.print("MOUNTED");

    display.display();

    scrollStatus();
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