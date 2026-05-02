/**
 * Implementation of WQV-1 protocol by @partlyhuman
 * Based on reverse engineering by https://www.mgroeber.de/wqvprot.html
 */
#include <FFat.h>

#include "config.h"
#include "display.h"
#include "firmware.h"
#include "frame.h"
#include "image.h"
#include "irda_hal.h"
#include "log.h"
#include "msc.h"

#ifdef ENABLE_PSRAM
#include "PSRamFS.h"
#endif

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

volatile static bool buttonPressed;
void IRAM_ATTR onButtonPress() {
    buttonPressed = true;
}

void setup() {
    Firmware::init();
    Display::init();
    Display::showBootScreen();

    Serial.begin(115200);
    // Doing this means it doesn't start until serial connected?
    // while (!Serial);

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LED_OFF);

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

    // Button manually toggles between Sync/USB (IR/MSC) mode and switches firmwares
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    attachInterrupt(PIN_BUTTON, onButtonPress, FALLING);

#ifdef PIN_IRDA_SD
    // TFDU4101 Shutdown pin, powered by bus power, safe to tie to ground
    pinMode(PIN_IRDA_SD, OUTPUT);
    digitalWrite(PIN_IRDA_SD, LOW);
    delay(1);
#endif

    IRDA_setup(IRDA);
    while (!IRDA);

    LOGI(TAG, "Setup complete");

    delay(2500);
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
 * Attempt to send a frame (from this device to the watch) and receive a frame in response. Every send has a response.
 * Will not return true unless a VALID frame is received (checksum must match, etc.)
 * Postcondition: after this method returns true, the globals addr, ctrl will be set to received values; the global
 * readBuffer will contain the DATA field of the frame after unescaping etc, and the global dataLen will contain the
 * length of valid data in that buffer.
 */
bool sendRetry(uint8_t a, uint8_t c, const uint8_t *d = nullptr, size_t l = 0, int retries = 5) {
    for (int retry = 0; retry < retries; retry++) {
        digitalWrite(PIN_LED, LED_OFF);
        Frame::writeFrame(a, c, d, l);
        // Important: don't do any heavy work here or you'll miss the beginning of the buffer
        digitalWrite(PIN_LED, LED_ON);
        len = IRDA.readBytesUntil(Frame::FRAME_EOF, readBuffer, BUFFER_SIZE);

        if (len <= 0) {
            LOGW(TAG, "Timeout, retrying %d...", retry);
            continue;
        }
        if (len == BUFFER_SIZE) {
            LOGE(TAG, "Filled buffer up all the way, probably dropping content");
            return false;
        }

        if (!Frame::parseFrame(readBuffer, len, dataLen, addr, ctrl)) {
            LOGW(TAG, "Malformed, retrying %d...", retry);
            continue;
        }
        return true;
    }
    return false;
}

/**
 * The first phase of any IR sync is opening the session. Performs a handshake that determines the "address" or "session
 * id", and prepares the watch to receive commands.
 * The open session is different in that the watch will send some responses multiple times (in response to subsequent
 * commands) before continuing to acknowledge the next command.
 * We also exit early if there is no reply to the first "hello?" send. This allows the caller to decide how frequently
 * to poll for a watch being here.
 */
bool openSession() {
    Display::showIdleScreen();

    sessionId = 0xff;

    // Set artificially low timeout for connecting so we can retry it frequently for faster connection
    IRDA.setTimeout(200);
    // >	FFh	B3h	(possibly repeated)
    sendRetry(0xff, 0xb3, nullptr, 0, 1);
    // Frame::writeFrame(0xff, 0xb3);
    // if (!readFrame()) return false;
    // <	FFh	A3h	<hh> <mm> <ss> <ff>
    if (!expect(0xff, 0xa3, 4)) {
        return false;
    }

    // Restore default timeout of 1sec after initial searching
    IRDA.setTimeout(1000);
    Display::dim(false);

    // We don't use the returned time. Maybe a future implementation could... tell you if your watch's time needs to be
    // adjusted? Except this device doesn't know the correct time either.

    // Generate a random session ID. Should avoid any escaped codes. 0 seems fine but we disallow it anyway.
    // No actual need to ensure its uniqueness.
    sessionId = (rand() % 0x1a) + 1;
    // echo back data adding the session ID to the end <hh> <mm> <ss> <ff><assigned address>
    readBuffer[4] = sessionId;

    Display::showConnectingScreen(0);

    for (size_t i = 0;;) {
        // >	FFh	93h	<hh> <mm> <ss> <ff><assigned address>
        sendRetry(0xff, 0x93, readBuffer, 5, 1);
        // <	<adr>	63h	(possibly repeated)
        if (expect(sessionId, 0x63)) break;
        if (++i > ABORT_AFTER_RETRIES) return false;
    }

    Display::showConnectingScreen(1);

    for (size_t i = 0;;) {
        // >	<adr>	11h
        sendRetry(sessionId, 0x11);
        // <	<adr>	01h
        if (expect(sessionId, 0x01)) break;
        if (++i > ABORT_AFTER_RETRIES) return false;
    }

    Display::showConnectingScreen(2);

    LOGI(TAG, "Handshake complete, connection established!");
    return true;
}

/**
 * Once a "download all images" synchronization sequence has been established by downloadImages(), performs the
 * download.
 * The images are presented as a flat array of these packed Image structs. Individual frames can include bytes from two
 * contiguous Images, so we focus on getting all the bytes (into a "dump") and then processing them afterwards. The dump
 * is stored in a file as it may be quite large for memory. This method doesn't care where the file is, just sees a
 * stream.
 * The download is performed by a sequence of "request address", "respond address data" pairs.  The "address" here is
 * not the actual byte offset but two repeating sequences, which is sufficient to guarantee ordering. Mgroeber calls
 * this "pumping" data.
 */
bool downloadToFile(size_t imgCount, Stream &stream) {
    const size_t IMAGE_SIZE = sizeof(Image::Image);

    // fixed initial packet sequence IDs
    uint8_t getPacketNum = 0x31;
    uint8_t retPacketNum = 0x42;
    for (size_t position = 0, retries = 0; position < IMAGE_SIZE * imgCount;) {
        if (retries > ABORT_AFTER_RETRIES) {
            LOGE(TAG, "Aborting sync after %d retries", ABORT_AFTER_RETRIES);
            return false;
        }

        // >    <adr> <getPacketNum>
        if (!sendRetry(sessionId, getPacketNum, nullptr, 0, 1)) {
            retries++;
            continue;
        }
        // <    <adr> <retPacketNum> 05h <data...>
        if (!expect(sessionId, retPacketNum)) {
            LOGW(TAG, "Mismatched Send %02x expect ret %02x, retrying", getPacketNum, retPacketNum);
            retries++;
            continue;
        }
        if (readBuffer[0] != 0x5) {
            LOGW(TAG, "Expected data to start with 0x05, retrying");
            retries++;
            continue;
        }
        retries = 0;

        // Append to file, skipping the initial 0x05
        position += stream.write(readBuffer + 1, dataLen - 1);

        // increment packet ids
        getPacketNum = getPacketNum + 0x20;  // natural wraparound 0xF1 + 0x20 = 0x11
        retPacketNum += 0x2;
        if (retPacketNum >= 0x50) retPacketNum = 0x40;  // cycles 40-4E,40-4E...

        // int curImg = offset / IMAGE_SIZE;
        // LOGI(TAG, "Progress: image %d/%d\t| %d bytes\t| %0.0f%%", curImg + 1, imgCount, offset,
        //      100.0f * offset / imgCount / IMAGE_SIZE);
        Display::showProgressScreen(position, IMAGE_SIZE * imgCount, IMAGE_SIZE);
    }

    LOGI(TAG, "Done reading all images!");
    return true;
}

/**
 * Establishes a full download of all images on the watch. The actual downloading is performed by downloadToFile() to a
 * file that we prepare for it. This handshaking will determine how many images are going to be received.
 */
bool downloadImages() {
    // >	<adr>	10h	01h
    static const uint8_t args_1[]{0x1};
    IRDA.flush();
    sendRetry(sessionId, 0x10, args_1, sizeof(args_1));
    // <	<adr>	21h
    if (!expect(sessionId, 0x21)) return false;

    Display::showConnectingScreen(3);

    // >	<adr>	11h
    sendRetry(sessionId, 0x11);
    // <	<adr>	20h	07h FAh 1Ch 3Dh <num_images>
    if (!expect(sessionId, 0x20, 5)) return false;

    Display::showConnectingScreen(4);

    size_t imgCount = readBuffer[4];
    LOGI(TAG, "Watch says %d images available", imgCount);
    // >	<adr>	32h	06h
    static const uint8_t args_6[]{0x6};
    sendRetry(sessionId, 0x32, args_6, sizeof(args_6));
    // <	<adr>	41h
    if (!expect(sessionId, 0x41)) return false;

    Display::showConnectingScreen(5);

    LOGI(TAG, "Upload preamble completed!");

    // Start every session with a fresh disk,
    // but don't wipe until we are just about to save, in order to allow access to previous export
    FFat.end();
    FFat.format(false);
    FFat.begin();

    size_t size = imgCount * sizeof(Image::Image);
    File dump;
#ifdef ENABLE_PSRAM
    if (usePsram)
        dump = PSRamFS.open(DUMP_PATH, FILE_WRITE);
    else
#endif
        dump = FFat.open(DUMP_PATH, FILE_WRITE);

    if (!dump) {
        LOGE(TAG, "Failed to allocate file %d bytes", size);
        return false;
    }
    downloadToFile(imgCount, dump);
    dump.close();

    return true;
}

/**
 * After a successful download, cleanly disconnects from the watch. After this is performed, the watch goes back into
 * the IR menu.
 */
bool closeSession() {
    LOGI(TAG, "Closing session...");
    // >	<adr>	54h	06h
    static const uint8_t args_6[]{0x6};
    sendRetry(sessionId, 0x54, args_6, sizeof(args_6));

    // <	<adr>	61h
    // TODO observing a number of different responses (0x61, 0x43,...) in real world here
    // LOGD(TAG, "Close session reply to 0x54 is 0x%02x followed by %d bytes, expected 0x61 or 0x43", ctrl, dataLen);
    if (!(expect(sessionId, 0x43) || expect(sessionId, 0x61))) return false;

    // >	<adr>	53h
    sendRetry(sessionId, 0x53);
    // <	<adr>	63h
    if (!expect(sessionId, 0x63)) return false;

    LOGI(TAG, "Successful close session handshake. Disconnected.");
    sessionId = 0xff;
    return true;
}

void loop() {
    if (buttonPressed) {
        ulong startTime = millis();
        ulong holdTime = 0;
        while (digitalRead(PIN_BUTTON) == LOW) {
            holdTime = millis() - startTime;
            if (holdTime > 500) {
                Firmware::rebootIntoNextPartition();
                return;
            }
        }
        Display::dim(false);
        if (MassStorage::active) {
            MassStorage::end();
        } else {
            Display::showMountedScreen();
            MassStorage::begin();
        }
        buttonPressed = false;
        return;
    }

    if (MassStorage::active) {
        return;
    }

    // Needed to clear after errors
    Display::showIdleScreen();
    if (openSession()) {
        if (downloadImages()) {
            // Don't require a clean disconnect to continue
            closeSession();

            File dump;
#ifdef ENABLE_PSRAM
            if (usePsram)
                dump = PSRamFS.open(DUMP_PATH, FILE_READ);
            else
#endif
                dump = FFat.open(DUMP_PATH, FILE_READ);
            Image::exportImagesFromDump(dump);
            dump.close();
            delay(250);  // Give the 100% screen some time to register

            Display::showMountedScreen();
            Serial.println("\n\nAttaching mass storage device, go look for your images!");
            Serial.println("Don't forget to eject when done!");

            Serial.flush();
            MassStorage::active = true;
            MassStorage::begin();
            return;
        }
    }

    // LOGE(TAG, "Failure or no watch present, restarting from handshake");
    // delay(1000);
}
