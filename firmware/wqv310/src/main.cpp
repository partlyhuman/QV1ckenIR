/**
 * Implementation of WQV-1 protocol by @partlyhuman
 * Based on reverse engineering by https://www.mgroeber.de/wqvprot.html
 */
#include <FFat.h>

static_assert(__cplusplus >= 202002L, "C++20 required for std::span");

#include <array>
#include <span>
#include <string>

#include "config.h"
#include "display.h"
#include "frame.h"
#include "image.h"
#include "irda_hal.h"
#include "log.h"
#include "msc.h"

#ifdef ENABLE_PSRAM
#include "PSRamFS.h"
#endif

using namespace std;

static const char *TAG = "Main";
static const char *DUMP_PATH = "/dump.bin";

const size_t ABORT_AFTER_RETRIES = 50;
const size_t MAX_IMAGES = 100;
// Packets seem to be up to 192 bytes
const size_t BUFFER_SIZE = 256;
static uint8_t readBuffer[BUFFER_SIZE]{};
// Transmission buffer and state variables
static uint8_t sessionId = 0xff;
static size_t len;
static size_t dataLen;
static uint8_t addr;
static uint8_t ctrl;
// Whether we're streaming the transferred data into PSRAM or FAT
static bool usePsram;
volatile static bool pendingManualModeToggle;

void IRAM_ATTR onManualModeToggleButton() {
    pendingManualModeToggle = true;
}

void setup() {
    Serial.begin(BAUDRATE);

    // Doing this means it doesn't start until serial connected?
    // while (!Serial);

    pinMode(PIN_LED, OUTPUT);

    // Should be a little under 1mb. PSRamFS will use heap if not available, we want to prevent that though
    usePsram = false;
#ifdef ENABLE_PSRAM
    size_t psramSize = MAX_IMAGES * sizeof(Image::Image) + 1024;
    if (psramInit() && psramSize < ESP.getMaxAllocPsram() && psramSize < ESP.getFreePsram()) {
        usePsram = PSRamFS.setPartitionSize(psramSize) && PSRamFS.begin(true);
    }
#endif
    LOGD(TAG, "Using %s for temp storage", usePsram ? "PSRAM" : "FFAT");

    MassStorage::init();
    Image::init();
    Display::init();

    // Button manually toggles between Sync/USB (IR/MSC) mode
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    attachInterrupt(PIN_BUTTON, onManualModeToggleButton, FALLING);

#ifdef PIN_IRDA_SD
    // TFDU4101 Shutdown pin, powered by bus power, safe to tie to ground
    pinMode(PIN_IRDA_SD, OUTPUT);
    digitalWrite(PIN_IRDA_SD, LOW);
    delay(1);
#endif

    IRDA_setup(IRDA);
    while (!IRDA);

    digitalWrite(PIN_LED, LED_OFF);
    LOGI(TAG, "Setup complete");
}

/**
 * Small helper to validate that a received frame has the expected properties (the addr & control fields).
 * Checking the length of the data is optional. Checking the contents of the data is out of scope.
 */
static inline bool expect(uint8_t expectedAddr, uint8_t expectedCtrl, int expectedMinLength = -1) {
    if (addr != expectedAddr) {
        LOGD(TAG, "Expected addr=%02x got addr=%02x\n", expectedAddr, addr);
        return false;
    }
    if (ctrl != expectedCtrl) {
        LOGD(TAG, "Expected ctrl=%02x got ctrl=%02x\n", expectedCtrl, ctrl);
        return false;
    }
    if (expectedMinLength >= 0 && len < expectedMinLength) {
        LOGD(TAG, "Expected at least %d bytes of data, got %d\n", expectedMinLength, len);
        return false;
    }
    return true;
}

/**
 * After a successful download, cleanly disconnects from the watch. After this is performed, the watch goes back into
 * the IR menu.
 */
bool closeSession() {
    LOGI(TAG, "Closing session...");
    return true;
}

bool readFrame(unsigned long timeout = 1000) {
    IRDA.setTimeout(timeout);
    len = IRDA.readBytesUntil(Frame::FRAME_EOF, readBuffer, BUFFER_SIZE);
    if (len == 0) {
        LOGW(TAG, "Timeout...");
        return false;
    }
    if (len == BUFFER_SIZE) {
        LOGE(TAG, "Filled buffer up all the way, probably dropping content");
        return false;
    }
    if (!Frame::parseFrame(readBuffer, len, dataLen, addr, ctrl)) {
        LOGW(TAG, "Malformed");
        return false;
    }
    return true;
}

template <typename First, typename... Rest>
auto concat(const First &first, const Rest &...rest) {
    using T = typename First::value_type;
    std::vector<T> result;

    size_t totalSize = first.size() + (rest.size() + ...);
    result.reserve(totalSize);

    result.insert(result.end(), first.begin(), first.end());
    (result.insert(result.end(), rest.begin(), rest.end()), ...);

    return result;
}

constexpr static array<uint8_t, 5> SVC{0x01, 0x0E, 0x03, 0x00, 0x00};
constexpr static array<uint8_t, 4> PAD4{0xFF, 0xFF, 0xFF, 0xFF};
bool openSession() {
    array<uint8_t, 12> HELLO{0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00};

    auto send = concat(SVC, PAD4, array<uint8_t, 3>{0x01, 0x00, 0x00});

    for (uint8_t count = 0; count < 6; count++) {
        send[send.size() - 2] = count;
        Frame::writeFrame(0xff, 0x3f, send, 11);
        dataLen = 0;
        if (readFrame(200) && expect(0xfe, 0xbf, 1)) break;
    }

    if (!dataLen) return false;

    // What watch is connecting?
    // 01193666 BED60300 00010500 84040043 4153494F 20574943 20323431 322F4952
    //                                     C A S I  O   W I  C 2 4 1  2 / I R
    // 01193666 BE0E0300 00010500 8C040043 4153494F 20574943 20323431 312F4952
    //                                     C A S I  O   W I  C 2 4 1  1 / I R

    auto watchIdString = string(reinterpret_cast<const char *>(readBuffer + 15), 17);
    LOGI(TAG, "Connected to watch '%s'", watchIdString.c_str());

    if (watchIdString == "CASIO WIC 2411/IR") {
        LOGI(TAG, "WQV-3 mode!");
    } else if (watchIdString == "CASIO WIC 2412/IR") {
        LOGI(TAG, "WQV-10 MODE!");
    } else {
        LOGI(TAG, "Unrecognized watch");
        return false;
    }

    return true;
}

void loop() {
    if (pendingManualModeToggle) {
        pendingManualModeToggle = false;
        Display::dim(false);
        if (MassStorage::active) {
            MassStorage::end();
        } else {
            Display::showMountedScreen();
            delay(500);
            MassStorage::begin();
        }
        return;
    }

    if (MassStorage::active) {
        return;
    }

    // Needed to clear after errors
    Display::showIdleScreen();
    if (openSession()) {
        delay(10000);
    } else {
        delay(1000);
    }

    // LOGE(TAG, "Failure or no watch present, restarting from handshake");
    // delay(1000);
}
