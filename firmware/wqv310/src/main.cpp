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

struct __attribute__((packed)) Timestamp {
    uint8_t year2k;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
};

static const char *TAG = "Main";
static const char *DUMP_PATH = "/dump.bin";

const size_t ABORT_AFTER_RETRIES = 50;
const size_t MAX_IMAGES = 100;
// Packets seem to be up to 192 bytes
const size_t BUFFER_SIZE = 1024;
static uint8_t readBuffer[BUFFER_SIZE];

static size_t len;
static size_t dataLen;
static uint8_t port;
static uint8_t hostPort = 0xff;
static uint8_t watchPort = 0xff;
static uint8_t seq;
static uint8_t session;

enum SequenceType {
    SEQ_ACK,
    SEQ_DATA,
};

uint8_t seq_upper = 1, seq_lower = 0;
SequenceType lastType;

inline uint8_t makeseq(SequenceType type, bool incUpper = false, bool incLower = false) {
    if (incUpper) seq_upper += 0x2;
    if (incLower) seq_lower += 0x2;
    return (seq_upper & 0xf) << 4 | ((type == SEQ_ACK ? 1 : seq_lower) & 0xf);
}

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
 * Small helper to validate that a received frame has the expected properties (the port & control fields).
 * Checking the length of the data is optional. Checking the contents of the data is out of scope.
 */
static inline bool expect(uint8_t expectedport, uint8_t expectedseq, int expectedMinLength = -1) {
    if (port != expectedport) {
        LOGD(TAG, "Expected port=%02x got port=%02x\n", expectedport, port);
        return false;
    }
    if (seq != expectedseq) {
        LOGD(TAG, "Expected seq=%02x got seq=%02x\n", expectedseq, seq);
        return false;
    }
    if (expectedMinLength >= 0 && len < expectedMinLength) {
        LOGD(TAG, "Expected at least %d bytes of data, got %d\n", expectedMinLength, len);
        return false;
    }
    return true;
}

static inline bool expectAck() {
    if (port != watchPort) {
        LOGD(TAG, "Expected port=%02x got port=%02x\n", watchPort, port);
        return false;
    }
    if ((seq & 0xF) != 1) {
        LOGD(TAG, "Expected seq=X1 (ACK) got seq=%02x\n", seq);
        return false;
    }
    if (dataLen != 0) {
        LOGD(TAG, "Expected empty data, got %d\n", len);
        return false;
    }
    return true;
}

bool readFrame(unsigned long timeout = 1000) {
    len = 0;
    dataLen = 0;

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
    bool parseOk = Frame::parseFrame(readBuffer, len, dataLen, port, seq);
    if (!parseOk) {
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

void appendSpan(vector<uint8_t> vec, span<uint8_t> data) {
    vec.insert(vec.end(), data.begin(), data.end());
}

template <typename T>
void concatStruct(vector<uint8_t> vec, T obj) {
    auto *begin = reinterpret_cast<const uint8_t *>(&obj);
    auto *end = begin + sizeof(T);
    vec.insert(vec.end(), begin, end);
}

bool openSession() {
    static constexpr array<uint8_t, 1> IRDA_STACK_CMD{0x01};
    static constexpr array<uint8_t, 4> PAD4{0xFF, 0xFF, 0xFF, 0xFF};
    static array<uint8_t, 4> IRDA_STACK_IDENT;

    // Randomize session string (Avoid 0xC? and 0x41)
    generate(IRDA_STACK_IDENT.begin(), IRDA_STACK_IDENT.end(), [] { return esp_random() & ~0b11000001; });
    // Randomize sip endpoints
    watchPort = esp_random() & 0xBE;
    hostPort = watchPort + 1;

    auto send = concat(IRDA_STACK_CMD, IRDA_STACK_IDENT, PAD4, array<uint8_t, 3>{0x01, 0x00, 0x00});
    for (uint8_t count = 0; count < 6; count++) {
        send[send.size() - 2] = count;
        Frame::writeFrame(0xff, 0x3f, send, 11);
        if (readFrame(25) && expect(0xfe, 0xbf, 1)) break;
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

    array<uint8_t, 27> START_SESSION{0x19, 0x36, 0x66, 0xBE, watchPort, 0x01, 0x01, 0x02, 0x82,
                                     0x01, 0x01, 0x83, 0x01, 0x3F,      0x84, 0x01, 0x0F, 0x85,
                                     0x01, 0x80, 0x86, 0x02, 0x80,      0x03, 0x08, 0x01, 0x07};

    send = concat(IRDA_STACK_IDENT, START_SESSION);
    // TODO i think we can 1. use our port now, and 2. init our own sequence state machine here
    // ACTUALLY it seems like maybe the reply is dependent on what seq we send??? > 93 < 73
    // > 23 no reply
    // But we actually did start earlier with 3f but the logs all really start that way
    Frame::writeFrame(0xff, 0x93, send, 5);
    if (!readFrame()) return false;
    LOGI(TAG, "Watch accepted SIP port %02x and replied with seq %02x", watchPort, seq);

    // Now we initialize the sequence state machine
    seq_upper = 1;
    seq_lower = 0;

    static const uint8_t SESSION_INIT_BEGIN[]{0x80, 0x03, 0x01, 0x00};
    Frame::writeFrame(hostPort, makeseq(SEQ_DATA), SESSION_INIT_BEGIN);
    // Expect an 83 back
    if (!readFrame()) return false;

    // 0x80030201 -- let's negotiate a session?
    const uint8_t SESSION_INIT[]{0x80, 0x03, 0x02, 0x01};
    Frame::writeFrame(hostPort, makeseq(SEQ_DATA, true, true), SESSION_INIT);
    // Expect ACK
    if (!(readFrame() && expectAck())) return false;

    // Generate session ID! Needs to be >3 <=F. Let's just use 4 for now
    // session = 7;
    session = random(0x4, 0xf);
    // session = esp_random() & 0xf;
    // if (session == 0) session++;

    // 0x830401000E -- assign session 04
    const uint8_t SESSION_IDENT[]{0x83, session, 0x01, 0x00, 0x0E};
    Frame::writeFrame(hostPort, makeseq(SEQ_DATA, false, true), SESSION_IDENT);
    // Expect  0x8403810001 (first byte = 0x80 | session)
    if (!readFrame()) return false;

    LOGI(TAG, "Confirmed session id %02x by %02x", session, readBuffer[1]);

    // > 0xB1
    Frame::writeFrame(hostPort, makeseq(SEQ_ACK, true, false));
    // expect ack
    if (!readFrame()) return false;

    // THIS DOES APPEAR TO START A RECONNECT?
    // > 0x0308000001
    const uint8_t IDK2[]{0x03, session, 0x00, 0x00, 0x01};
    Frame::writeFrame(hostPort, makeseq(SEQ_DATA, false, true), IDK2);
    // < 0x0803010C100400002580110103000104
    readFrame();
    // > 0xD1
    Frame::writeFrame(hostPort, makeseq(SEQ_ACK, true, false));
    // < 0x080300000D
    readFrame();
    // > 0xF1
    Frame::writeFrame(hostPort, makeseq(SEQ_ACK, true, false));
    readFrame();

    // // Is 0x7d sometimes there and sometimes not???
    // const uint8_t SESSION_INIT_END[]{0x80, 0x03, 0x01, 0x00, 0x7D};
    // Frame::writeFrame(hostPort, makeseq(SEQ_DATA, true, true), SESSION_INIT_END);
    // if (!readFrame()) return false;

    return true;
}

bool ping() {
    Frame::writeFrame(hostPort, makeseq(SEQ_ACK));
    return (readFrame() && expectAck());
}

bool closeSession() {
    LOGI(TAG, "Closing session...");
    // 0x83 SS 0201
    const uint8_t HANGUP[]{0x83, session, 0x02, 0x01};
    Frame::writeFrame(hostPort, makeseq(SEQ_DATA, false, true), HANGUP);
    readFrame();
    Frame::writeFrame(hostPort, makeseq(SEQ_ACK, false, true));
    readFrame();
    Frame::writeFrame(hostPort, 0x53);
    readFrame();  // replies with 73
    // Frame::writeFrame(hostPort, 0x73);
    return true;
}

void endSessionSwapRoles() {
    LOGI(TAG, "Ending session and swapping roles");

    // Possibilities: (session = 08)
    // 0x030806
    // const uint8_t SWAP[]{0x03, session, 0x06};
    // Frame::writeFrame(hostPort, makeseq(SEQ_DATA, false, true), SWAP);

    // ping();

    closeSession();

    LOGI(TAG, "Let's see if we hear anything back from watch");
    readFrame(10000);
}

bool setupSyncSession() {
    LOGI(TAG, "Setting up sync session...");
    const uint8_t START_SYNC_SESSION[]{0x19, 0x36, 0x66, 0xBE, 0x01, 0x03, 0x00, 0x00, 0x70, 0x01,
                                       0x01, 0x3F, 0x82, 0x01, 0x01, 0x83, 0x01, 0x3F, 0x84, 0x01,
                                       0x01, 0x85, 0x01, 0x80, 0x86, 0x01, 0x03, 0x08, 0x01, 0xFF};
    Frame::writeFrame(0xff, 0x93, START_SYNC_SESSION);
    readFrame();

    ping();

    const uint8_t S0[]{0x81, 0, 0x81, 0};
    Frame::writeFrame(hostPort, makeseq(SEQ_DATA, true, true), S0);
    // ??
    readFrame();

    const uint8_t S1[]{0x83, session, 0x81, 0x00, 0x0E};
    // > 0x830781000E
    Frame::writeFrame(hostPort, makeseq(SEQ_DATA, true, true), S1);
    // < 0x07030003000104
    readFrame();
    // > ACK
    Frame::writeFrame(hostPort, makeseq(SEQ_ACK, true, true));
    // < 0x0703000C10040000258011010320010C
    readFrame();

    // keep ack-ing until we get more from the watch
    while (ping());

    // we should have something in the data now
    // <
    // 0x070300000010000101341004000000000000000008007403000000001166723A330D0A69643A434153494F2057494320323431312F495220202020200D0A10020000
    // get 26 bytes starting at 30
    if (dataLen < 56) {
        LOGE(TAG, "Expected at least 56 bytes, got %d", dataLen);
        return false;
    }
    auto syncIdString = string(reinterpret_cast<const char *>(readBuffer + 30), 26);
    LOGI(TAG, "Connected to sync endpoint '%s'", syncIdString.c_str());

    for (int i = 0; i < 10; i++) ping();

    const uint8_t SYNC_ID[]{0x03, session, 0x00, 0x00, 0x00, 0x11, 0x01, 0x30, 0x10, 0x04, 0x08, 0x00, 0x74, 0x03,
                            0x00, 0x00,    0x00, 0x00, 0x08, 0x00, 0x74, 0x03, 0x10, 0x00, 0x00, 0x00, 0x11, 0x66,
                            0x72, 0x3A,    0x31, 0x0D, 0x0A, 0x69, 0x64, 0x3A, 0x4C, 0x49, 0x4E, 0x4B, 0x20, 0x51,
                            0x57, 0x32,    0x34, 0x31, 0x31, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x0D, 0x0A};
    Frame::writeFrame(hostPort, makeseq(SEQ_DATA, true, true), SYNC_ID);
    readFrame();

    LOGI(TAG, "That's all i have so far");
    return true;
}

void page(int pageDir) {
    if (pageDir == 0) return;
    array<uint8_t, 5> PAGE_FWD{0x03, session, 0x00, 0x00, 0x02};
    array<uint8_t, 5> PAGE_BACK{0x03, session, 0x00, 0x00, 0x03};
    Frame::writeFrame(hostPort, makeseq(SEQ_DATA, false, true), pageDir > 0 ? PAGE_FWD : PAGE_BACK);
    readFrame();
    Frame::writeFrame(hostPort, makeseq(SEQ_ACK, true, false));
    readFrame();
}

void syncTime() {
    array<uint8_t, 5> SEND_TIME{0x03, 0x07, 0x00, 0x00, 0x07};

    vector<uint8_t> send;
    send.reserve(SEND_TIME.size() + sizeof(Timestamp));
    send.insert(send.begin(), SEND_TIME.begin(), SEND_TIME.end());

    Timestamp time = {.year2k = 20, .month = 1, .day = 30, .hour = 12, .minute = 30};
    // concatStruct(send, time);
    auto *begin = reinterpret_cast<const uint8_t *>(&time);
    auto *end = begin + sizeof(Timestamp);
    send.insert(send.end(), begin, end);

    Frame::writeFrame(hostPort, 0xBE, send, 5);
    if (readFrame() && expect(watchPort, 0x1A)) {
        LOGI(TAG, "Accepted time?");
    }
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
        // setupSyncSession();
        for (int i = 0; i < 5; i++) {
            ping();
            delay(100);
        }

        endSessionSwapRoles();

        // delay(25);
        // syncTime();

        // for (int j = 0; j < 10; j++) {
        //     for (int i = 0; i < 5; i++) {
        //         ping();
        //         delay(200);
        //     }
        //     page(1);
        //     delay(100);
        // }

        closeSession();
        delay(10000);
    } else {
        delay(1000);
    }

    // LOGE(TAG, "Failure or no watch present, restarting from handshake");
    // delay(1000);
}
