/**
 * Use the QV1ckenIR as an IR dongle over USB serial, and log everything
 */

#include <Arduino.h>
#include <Preferences.h>

#include "FFat.h"
#include "config.h"
#include "irda_hal.h"
#include "msc.h"

Preferences prefs;

static const size_t SERIAL_BUFFER_SIZE = 4096;
static uint8_t SERIAL_BUFFER[SERIAL_BUFFER_SIZE]{};

static File logFile;
static int logFileCount = 0;

static uint8_t const MARK_RX[]{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00};
static uint8_t const MARK_TX[]{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00};
static uint32_t lastLogTime = 0;
static bool lastLogWasTx = true;
const uint32_t NEW_LOG_AFTER_IDLE_MS = 1000;

void onManualModeToggleButton() {
    if (!MassStorage::media_present && logFile) {
        logFile.flush();
        logFile.close();
    }
    MassStorage::toggle();
}

void setup() {
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LED_OFF);

    USBSerial.begin(BAUDRATE);
    // Attempt to avoid the DTS/RTS reboot
    USBSerial.enableReboot(false);
    USBSerial.setDebugOutput(false);

    // Doing this means it doesn't start until serial connected?
    // while (!USBSerial);

#ifdef PIN_IRDA_SD
    // TFDU4101 Shutdown pin, powered by bus power, safe to tie to ground
    pinMode(PIN_IRDA_SD, OUTPUT);
    digitalWrite(PIN_IRDA_SD, LOW);
    delay(1);
#endif

    IRDA_setup(IRDA);
    while (!IRDA);

    prefs.begin("prefs");
    logFileCount = prefs.getUInt("logFileCount");

    MassStorage::init();
    MassStorage::begin();

    attachInterrupt(PIN_BUTTON, onManualModeToggleButton, FALLING);
}

void cycleLog() {
    uint32_t now = millis();
    if (logFile && now > lastLogTime + NEW_LOG_AFTER_IDLE_MS) {
        logFile.close();
    }
}

void log(bool isTx, uint8_t *buf, int len) {
    if (MassStorage::media_present) return;

    uint32_t now = millis();
    if (!logFile) {
        char filename[64] = "";
        sprintf(filename, "/%05d.bin", logFileCount++);
        prefs.putUInt("logFileCount", logFileCount);
        logFile = FFat.open(filename, "w", true);
        lastLogWasTx = true;
    }

    if (lastLogWasTx != isTx) {
        logFile.write(isTx ? MARK_TX : MARK_RX, sizeof(MARK_TX));
        lastLogWasTx = isTx;
    }

    logFile.write(buf, len);
    lastLogTime = now;
}

void loop() {
    cycleLog();

    // TX
    size_t avail = USBSerial.available();
    if (avail > 0) {
        // Flash on TX
        // digitalWrite(LED_BUILTIN, LED_ON);
        avail = USBSerial.readBytes(SERIAL_BUFFER, min(avail, SERIAL_BUFFER_SIZE));
        IRDA_tx(true);
        IRDA.write(SERIAL_BUFFER, avail);
        IRDA.flush();
        IRDA_tx(false);
        log(true, SERIAL_BUFFER, avail);
    }

    // RX
    avail = IRDA.available();
    if (avail > 0) {
        // Flash on RX
        digitalWrite(LED_BUILTIN, LED_ON);
        avail = IRDA.readBytes(SERIAL_BUFFER, min(avail, SERIAL_BUFFER_SIZE));
        USBSerial.write(SERIAL_BUFFER, avail);
        USBSerial.flush();
        log(false, SERIAL_BUFFER, avail);
    }

    digitalWrite(LED_BUILTIN, LED_OFF);
}
