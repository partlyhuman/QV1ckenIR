// Based on a gist by @frank26080115. Thank you!
// https://gist.githubusercontent.com/frank26080115/82e2b4256883604016e177bdd78790c2/raw/2c962bcb94932355c75bc30ba39484443a750226/usbmsc_fat_demo.ino

#include "msc.h"

#include <Arduino.h>

#include "FFat.h"
#include "FS.h"
#include "USB.h"
#include "USBMSC.h"
#include "config.h"
#include "log.h"

#define READONLY
#define MSC_VENDOR_ID "Partlyhuman"
#define MSC_PRODUCT_ID "WQV-1 Interface"
#define MSC_PRODUCT_REVISION "1.0"

namespace MassStorage {

static const char* TAG = "MassStorage";

USBMSC MSC;

wl_handle_t flash_handle;
uint32_t sect_size;
uint32_t total_size;
uint32_t sect_cnt;

void shutdown() {
    MSC.end();
    FFat.end();
}

void usbEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    // So far haven't seen this called, probably when USB disconnected/connected,
    // But in our application this is going to also provide & cut power, so what would you even do
    // Serial.printf("EVENT base=%d id=%d\n", event_base, event_id);
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
#ifndef READONLY
    wl_write(flash_handle, (size_t)((lba * sect_size) + offset), (const void*)buffer, (size_t)bufsize);
#endif

    // flash on writes
    static uint8_t f = HIGH;
    digitalWrite(LED_BUILTIN, f);
    f = (f == HIGH) ? LOW : HIGH;
    // TODO something so it doesn't stay on - handle loop, or vxtask/watchdog

    return bufsize;
}

int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    wl_read(flash_handle, (size_t)((lba * sect_size) + offset), (void*)buffer, (size_t)bufsize);
    return bufsize;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    // Without this, drive just keeps coming back
    if (!start && load_eject) {
        MSC.mediaPresent(false);
        delay(250);

        // Maybe, maybe not
        shutdown();
        ESP.restart();
    }
    return true;
}

void init() {
    if (!FFat.begin(true)) {
        return;
    }

    // flash_handle = FFat._wl_handle;  // FFat.h is hacked to made this public
    sect_size = wl_sector_size(flash_handle);
    total_size = wl_size(flash_handle);
    sect_cnt = total_size / sect_size;

    USB.onEvent(usbEventCallback);
    MSC.vendorID(MSC_VENDOR_ID);
    MSC.productID(MSC_PRODUCT_ID);
    MSC.productRevision(MSC_PRODUCT_REVISION);
    MSC.onRead(onRead);
    MSC.onWrite(onWrite);
    MSC.onStartStop(onStartStop);
    MSC.mediaPresent(false);
    MSC.begin(sect_cnt, sect_size);
    USB.begin();
}

void begin() {
    digitalWrite(PIN_LED, LED_OFF);
    Serial.end();
    MSC.mediaPresent(true);
}

void end() {
    MSC.mediaPresent(false);
    Serial.begin(115200);
}

}  // namespace MassStorage