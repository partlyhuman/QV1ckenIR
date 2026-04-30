#include <FFat.h>

#include <cstring>

#include "PSRamFS.h"
#include "chunk.h"
#include "config.h"
#include "display.h"
#include "frame.h"
#include "image.h"
#include "irda_hal.h"
#include "log.h"
#include "msc.h"
#include "stl_helpers.h"
#include "types.h"

// TODO undo this
using namespace Frame;

static const char *TAG = "Main";

const size_t ABORT_AFTER_RETRIES = 50;
const size_t MAX_IMAGES = 100;
// Packets seem to be up to 192 bytes
const size_t BUFFER_SIZE = 1024;
static uint8_t readBuffer[BUFFER_SIZE];

static size_t len;
static size_t dataLen;
static uint8_t port;
static uint8_t ourPort = 0xff;
static uint8_t watchPort = 0xff;
static uint8_t seq;
static uint8_t session;

enum SequenceType {
    SEQ_ACK,
    SEQ_DATA,
};

uint8_t seq_upper = 1, seq_lower = 0;

// TODO simplify this, it should really inc lower only when data type, and know the difference between ack and ack loop
inline uint8_t makeseq(SequenceType type, bool incUpper = false, bool incLower = false) {
    if (incUpper) seq_upper += 0x2;
    if (incLower) seq_lower += 0x2;
    return (seq_upper & 0xf) << 4 | ((type == SEQ_ACK ? 1 : seq_lower) & 0xf);
}

volatile static bool pendingManualModeToggle;
void IRAM_ATTR onManualModeToggleButton() {
    pendingManualModeToggle = true;
}

void setup() {
    Serial.begin(BAUDRATE);

    // Doing this means it doesn't start until serial connected?
    // while (!Serial);

    pinMode(PIN_LED, OUTPUT);

    PSRamFS.begin(true);
    Image::init();
    MassStorage::init();
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
static bool expect(uint8_t expectedport, uint8_t expectedseq, int expectedMinLength = -1) {
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

static bool expectAck() {
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

template <size_t N>
static bool expectStartsWith(const uint8_t (&lit)[N]) {
    std::span buf(readBuffer);
    return buf.size() >= N && std::equal(buf.begin(), buf.begin() + N, lit);
}

// TODO we should have a higher-level thing that replies with a state 0x03 when we parse fail or resends last when
// timing out
bool readFrame(unsigned long timeout = 1000) {
    len = 0;
    dataLen = 0;

    IRDA.setTimeout(timeout);
    len = IRDA.readBytesUntil(FRAME_EOF, readBuffer, BUFFER_SIZE);
    if (len == 0) {
        LOGW(TAG, "Timeout...");
        return false;
    }
    if (len == BUFFER_SIZE) {
        LOGE(TAG, "Filled buffer up all the way, probably dropping content");
        return false;
    }
    bool parseOk = parseFrame(readBuffer, len, dataLen, port, seq);
    if (!parseOk) {
        LOGW(TAG, "Malformed");
        return false;
    }
    return true;
}

bool openSession() {
    static constexpr std::array<uint8_t, 1> IRDA_STACK_CMD{0x01};
    static constexpr std::array<uint8_t, 4> PAD4{0xFF, 0xFF, 0xFF, 0xFF};
    static std::array<uint8_t, 4> IRDA_RAND_STR;

    // Randomize session string (Avoid 0xC? and 0x41)
    generate(IRDA_RAND_STR.begin(), IRDA_RAND_STR.end(), [] { return esp_random() & ~0b11000001; });
    // Randomize sip endpoints
    watchPort = esp_random() & 0xBE;
    ourPort = watchPort + 1;

    auto send = concat(IRDA_STACK_CMD, IRDA_RAND_STR, PAD4, std::array<uint8_t, 3>{0x01, 0x00, 0x00});
    for (uint8_t count = 0; count < 6; count++) {
        send[send.size() - 2] = count;
        writeFrame(0xff, 0x3f, send, 11);
        if (readFrame(25) && expect(0xfe, 0xbf, 1)) break;
    }

    if (!dataLen) return false;

    Display::showConnectingScreen(0);

    // What watch is connecting?
    // 01193666 BED60300 00010500 84040043 4153494F 20574943 20323431 322F4952
    //                                     C A S I  O   W I  C 2 4 1  2 / I R
    // 01193666 BE0E0300 00010500 8C040043 4153494F 20574943 20323431 312F4952
    //                                     C A S I  O   W I  C 2 4 1  1 / I R

    auto watchIdString = std::string(reinterpret_cast<const char *>(readBuffer + 15), 17);
    LOGI(TAG, "Connected to watch '%s'", watchIdString.c_str());

    if (watchIdString == "CASIO WIC 2411/IR") {
        LOGI(TAG, "WQV-3 mode!");
    } else if (watchIdString == "CASIO WIC 2412/IR") {
        LOGI(TAG, "WQV-10 MODE!");
    } else {
        LOGI(TAG, "Unrecognized watch");
        return false;
    }

    std::array<uint8_t, 27> START_SESSION{0x19, 0x36, 0x66, 0xBE, watchPort, 0x01, 0x01, 0x02, 0x82,
                                          0x01, 0x01, 0x83, 0x01, 0x3F,      0x84, 0x01, 0x0F, 0x85,
                                          0x01, 0x80, 0x86, 0x02, 0x80,      0x03, 0x08, 0x01, 0x07};

    send = concat(IRDA_RAND_STR, START_SESSION);
    // TODO i think we can 1. use our port now, and 2. init our own sequence state machine here
    // ACTUALLY it seems like maybe the reply is dependent on what seq we send??? > 93 < 73
    // > 23 no reply
    // But we actually did start earlier with 3f but the logs all really start that way
    writeFrame(0xff, 0x93, send, 5);
    if (!readFrame()) return false;
    LOGI(TAG, "Watch accepted SIP port %02x and replied with seq %02x", watchPort, seq);

    Display::showConnectingScreen(1);

    // Now we initialize the sequence state machine
    seq_upper = 1;
    seq_lower = 0;

    static const uint8_t SESSION_INIT_BEGIN[]{0x80, 0x03, 0x01, 0x00};
    writeFrame(ourPort, makeseq(SEQ_DATA), SESSION_INIT_BEGIN);
    // Expect an 83 back
    if (!readFrame()) return false;

    // 0x80030201 -- let's negotiate a session?
    const uint8_t SESSION_INIT[]{0x80, 0x03, 0x02, 0x01};
    writeFrame(ourPort, makeseq(SEQ_DATA, true, true), SESSION_INIT);
    // Expect ACK
    if (!(readFrame() && expectAck())) return false;

    // Generate session ID! Needs to be >3 <=F
    session = random(0x4, 0xf);

    // 0x830401000E -- assign session 04
    const uint8_t SESSION_IDENT[]{0x83, session, 0x01, 0x00, 0x0E};
    writeFrame(ourPort, makeseq(SEQ_DATA, false, true), SESSION_IDENT);
    // Expect  0x8403810001 (first byte = 0x80 | session)
    if (!readFrame()) return false;

    LOGI(TAG, "Confirmed session id %02x by %02x", session, readBuffer[1]);

    // > 0xB1
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));
    // expect ack
    if (!readFrame()) return false;

    Display::showConnectingScreen(2);

    // TODO we can't reverse roles after going into this state?
    // // Is 0x7d sometimes there and sometimes not??? // 0x7d is escape so this can't be right without a subsequent
    // byte const uint8_t SESSION_INIT_END[]{0x80, 0x03, 0x01, 0x00, 0x7D}; writeFrame(ourPort, makeseq(SEQ_DATA, true,
    // true), SESSION_INIT_END); if (!readFrame()) return false;

    return true;
}

void ackLoopInClientRole(unsigned long timeout = 2000) {
    while (true) {
        bool ok = readFrame(timeout);

        if (ok && seq == 0x53) {
            LOGE(TAG, "Error status");
            return;
        }

        // LOGD(TAG, "Is this ack? port=%d watchport=%d dataLen=%d seq=%d seq&f=%d", port, watchPort, dataLen, seq,
        //      (seq & 0xf));
        if (!ok || (port == watchPort && dataLen == 0 && (seq & 0xf) == 1)) {
            // LOGD(TAG, "yeah, it's ack, we'll ack now");
            // ACK, we'll reply ACK
            writeFrame(ourPort, makeseq(SEQ_ACK, false, false));
        } else {
            // LOGD(TAG, "it's not, done");
            return;
        }
    }
}

bool openSessionInClientRole() {
    watchPort = 0xff;
    ourPort = 0xfe;
    seq_lower = 0;
    seq_upper = 1;

    Display::showConnectingScreen(3);

    // < 0x3F 0x01193666BEFFFFFFFF010000...
    // give first one a long time to arrive
    readFrame(5000);

    // for (int i = 1; dataLen > 0 && readBuffer[0] == 0x01 && readBuffer[1] == 0x19 && i < 6; i++) {
    //     // We should receive 6 IRDA hello frames
    //     readFrame(1500);
    // }
    // LOGD(TAG, "That should be all the IRDA stack hello frames");

    Display::showConnectingScreen(4);

    // > 0xBF 0x01(0E030000)193666BE01050084250057494E444F5753585000 "WINDOWS XP"
    // We replace that with QV1-BLINK
    uint8_t IRDA_HI[]{0x01, 0xff, 0xff, 0xff, 0xff, 0x19, 0x36, 0x66, 0xbe, 0x01, 0x05, 0x00, 0x84,
                      0x25, 0x00, 0x51, 0x56, 0x31, 0x2d, 0x42, 0x4c, 0x49, 0x4e, 0x4b, 0x00};
    for (int i = 1; i <= 4; i++) {
        IRDA_HI[i] = esp_random() & ~0b11000001;
    }
    writeFrame(ourPort, 0xbf, IRDA_HI);

    do {
        // < 0x3F 0x01193666BEFFFFFFFF010100
        // < 0x3F 0x01193666BEFFFFFFFF010200
        // < 0x3F 0x01193666BEFFFFFFFF010300
        // < 0x3F 0x01193666BEFFFFFFFF010400
        // < 0x3F 0x01193666BEFFFFFFFF010500
        readFrame();

        // < 0x3F 0x01193666BEFFFFFFFF01FF008C0400434153494F2057494320323431312F4952 ("casio wic...")
    } while (!expectStartsWith({0x01, 0x19, 0x36, 0x66, 0xBE, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0xFF}));

    std::string watchIdentString(reinterpret_cast<const char *>(readBuffer + 15), 17);
    LOGI(TAG, "IRDA stack host ident '%s'", watchIdentString.c_str());

    // < 0x93 0x193666BE0E030000(70)01013F82010183013F8401018501808601030801FF
    readFrame();
    if (readBuffer[0] == 0x19) {
        ourPort = readBuffer[8];
        watchPort = ourPort + 1;
        LOGI(TAG, "Accepting ports assigned by watch: device=%02x watch=%02x", ourPort, watchPort);
    }

    // > 0x73 0x0E030000193666BE01010282010183013F84010F85018086028003080104
    constexpr uint8_t CONNECT_STR[]{0x0e, 0x03, 0x00, 0x00, 0x19, 0x36, 0x66, 0xbe, 0x01, 0x01,
                                    0x02, 0x82, 0x01, 0x01, 0x83, 0x01, 0x3f, 0x84, 0x01, 0x0f,
                                    0x85, 0x01, 0x80, 0x86, 0x02, 0x80, 0x03, 0x08, 0x01, 0x04};
    writeFrame(ourPort, 0x73, CONNECT_STR);

    ackLoopInClientRole();

    // < 0x10 0x80010100
    // readFrame(); // ackLoop returned because this was already in the buffer
    // > 0x30 0x81008100
    constexpr uint8_t SESSION_INIT_0[]{0x81, 0x00, 0x81, 0x00};
    writeFrame(ourPort, makeseq(SEQ_DATA, true, false), SESSION_INIT_0);

    // Lots of IRDA garbage who cares
    // < 0x32 0x0001840B497244413A4972434F4D4D13497244413A54696E7954503A4C73617053656C
    readFrame();
    // if (dataLen > 0 && readBuffer[0] == 0 && readBuffer[1] == 0x01) {
    constexpr uint8_t IRDA_NEGOTIATE_0[]{0x01, 0x00, 0x84, 0x00, 0x00, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x09};
    // > 0x52 0x01008400000100050100000009
    writeFrame(ourPort, makeseq(SEQ_DATA, true, true), IRDA_NEGOTIATE_0);
    // }

    // < 0x54 0x0001840B497244413A4972434F4D4D0A506172616D65746572737D
    readFrame();
    // more IRDA garbage
    // > 0x74 0x0100840000010005020006000106010101
    constexpr uint8_t IRDA_NEGOTIATE_1[]{0x01, 0x00, 0x84, 0x00, 0x00, 0x01, 0x00, 0x05, 0x02,
                                         0x00, 0x06, 0x00, 0x01, 0x06, 0x01, 0x01, 0x01};
    writeFrame(ourPort, makeseq(SEQ_DATA, true, true), IRDA_NEGOTIATE_1);

    // < 0x76 0x8903010001
    readFrame();
    if (dataLen > 0 && (readBuffer[0] & 0xf0) == 0x80) {
        session = readBuffer[0] & 0xf;
        LOGI(TAG, "Received session id %01x from watch", session);
    } else {
        LOGE(TAG, "Missed session id, should have been able to figure it out");
        return false;
    }
    // > 0x96 0x830981000E
    uint8_t SESSION_CONFIRM[]{0x83, session, 0x81, 0x00, 0x0e};
    writeFrame(ourPort, makeseq(SEQ_DATA, true, true), SESSION_CONFIRM);

    // < 0x98 0x09030003000104
    readFrame();
    // > 0xB1
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));

    // < 0x9A 0x0903000C10040000258011010320010C
    readFrame();
    // > 0xD1
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));

    // loop while received frame is ack

    Display::showConnectingScreen(5);

    LOGD(TAG, "Done opening session in client role");
    return true;
}

bool ping() {
    writeFrame(ourPort, makeseq(SEQ_ACK));
    return (readFrame() && expectAck());
}

bool closeSession() {
    LOGI(TAG, "Closing session...");
    // 0x83 SS 0201
    const uint8_t HANGUP[]{0x83, session, 0x02, 0x01};
    writeFrame(ourPort, makeseq(SEQ_DATA, false, true), HANGUP);
    readFrame();
    writeFrame(ourPort, makeseq(SEQ_ACK, false, true));
    readFrame();
    writeFrame(ourPort, 0x53);
    readFrame();  // replies with 73
    // writeFrame(ourPort, 0x73);
    return true;
}

bool swapRolesAndCloseSession() {
    LOGI(TAG, "Ending session and swapping roles");

    // THIS DOES APPEAR TO START A RECONNECT WITH SWAPPED ROLES?
    // > 0x0308000001
    const uint8_t REQUEST_REVERSE_ROLES[]{0x03, session, 0x00, 0x00, 0x01};
    writeFrame(ourPort, makeseq(SEQ_DATA, false, true), REQUEST_REVERSE_ROLES);
    // < 0x0803010C100400002580110103000104
    readFrame();
    // > 0xD1
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));
    // < 0x080300000D
    readFrame();
    // > 0xF1
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));
    readFrame();

    closeSession();
    return true;
}

void page(int pageDir) {
    if (pageDir == 0) return;
    std::array<uint8_t, 5> PAGE_FWD{0x03, session, 0x00, 0x00, 0x02};
    std::array<uint8_t, 5> PAGE_BACK{0x03, session, 0x00, 0x00, 0x03};
    writeFrame(ourPort, makeseq(SEQ_DATA, false, true), pageDir > 0 ? PAGE_FWD : PAGE_BACK);
    readFrame();
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));
    readFrame();
}

void sendTime() {
    std::array<uint8_t, 5> SEND_TIME{0x03, session, 0x00, 0x00, 0x07};

    std::vector<uint8_t> send;
    send.reserve(SEND_TIME.size() + sizeof(Timestamp));
    send.insert(send.begin(), SEND_TIME.begin(), SEND_TIME.end());

    Timestamp time = {.year2k = 20, .month = 1, .day = 30, .hour = 12, .minute = 30};
    // appendStruct(send, time);
    auto *begin = reinterpret_cast<const uint8_t *>(&time);
    auto *end = begin + sizeof(Timestamp);
    send.insert(send.end(), begin, end);

    writeFrame(ourPort, 0xBE, send, 5);
    if (readFrame() && expect(watchPort, 0x1A)) {
        LOGI(TAG, "Accepted time?");
    }
}

std::vector<uint8_t> cmdToResponse(std::span<const uint8_t> src, int8_t shifts,
                                   std::span<const uint8_t> extraData = {}) {
    constexpr char RESPONSE_BDY0[] = "BDY0";
    std::vector<uint8_t> response(src.begin(), src.end());
    response.reserve(response.size() + 4 + 4 + extraData.size());

    // 1. First bytes: swap [session] 03 -> 03 [session]
    response[0] = 0x03;
    response[1] = session;

    // 2. Last byte 0x2b: 02 -> 01
    response[0x2b] = 0x01;

    // 3. Byte 0xf: 00 -> 40
    response[0xf] = 0x40;

    // 4. Byte 0x18 & 0x20: swap
    response[0x18] = src[0x20];
    response[0x20] = src[0x18];

    // 5. Byte 0x29 unchanged

    // 6. Shifted fields
    response[0x9] = static_cast<uint8_t>(static_cast<int8_t>(src[0x9]) + shifts);
    response[0xd] = static_cast<uint8_t>(static_cast<int8_t>(src[0xd]) + shifts);
    response[0x13] = static_cast<uint8_t>(static_cast<int8_t>(src[0x13]) + shifts);

    // Append "BDY0" without \0
    response.insert(response.end(), std::begin(RESPONSE_BDY0), std::end(RESPONSE_BDY0) - 1);

    // Append 4-byte size of extended data
    uint32_t extraSize = static_cast<uint32_t>(extraData.size());
    response.push_back(static_cast<uint8_t>((extraSize >> 24) & 0xFF));
    response.push_back(static_cast<uint8_t>((extraSize >> 16) & 0xFF));
    response.push_back(static_cast<uint8_t>((extraSize >> 8) & 0xFF));
    response.push_back(static_cast<uint8_t>(extraSize & 0xFF));

    // Append extended data
    response.insert(response.end(), extraData.begin(), extraData.end());
    return response;
}

std::string getCmdName() {
    return std::string(reinterpret_cast<const char *>(readBuffer + dataLen - 4), 4);
}

bool syncInClientRole() {
    std::vector<uint8_t> response;
    std::span<uint8_t> cmdSpan;

    // NOTE it's important to only do work when the watch is waiting for a reply
    readFrame();
    LOGI(TAG, "Formatting FatFS...");
    FFat.end();
    FFat.format();
    FFat.begin();
    writeFrame(ourPort, makeseq(SEQ_ACK, false, false));

    ackLoopInClientRole();
    // < 0x9C
    // 0x090300000010000101341004000000000000000008007403000000001166723A330D0A69643A434153494F2057494320323431312F495220202020200D0A10020000
    if (expectStartsWith({session, 0x03, 0x00, 0x00, 0x00, 0x10})) {
        // This one is null terminated
        auto watchFw = std::string(reinterpret_cast<const char *>(readBuffer + 0x26), 0x16);
        LOGI(TAG, "Identified as id:'%s'", watchFw.c_str());
    } else {
        return false;
    }
    // TODO could we skip the ack/ack?
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));

    if (!(readFrame() && expectAck())) return false;
    uint8_t FW_IDENT[]{0x03, session, 0x00, 0x00, 0x00, 0x11, 0x01, 0x30, 0x10, 0x04, 0x08, 0x00, 0x74, 0x03,
                       0x00, 0x00,    0x00, 0x00, 0x08, 0x00, 0x74, 0x03, 0x10, 0x00, 0x00, 0x00, 0x11, 0x66,
                       0x72, 0x3A,    0x31, 0x0D, 0x0A, 0x69, 0x64, 0x3A, 0x4C, 0x49, 0x4E, 0x4B, 0x20, 0x51,
                       0x57, 0x32,    0x34, 0x31, 0x31, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x0D, 0x0A};
    writeFrame(ourPort, makeseq(SEQ_DATA, false, true), FW_IDENT);

    // < 0xBE 0x090301
    ackLoopInClientRole();
    if (!expectStartsWith({session, 0x03, 0x01})) return false;
    LOGD(TAG, "<< Yield next object from watch");
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));

    // RIMG
    // < 0xB0
    // 0x09030000002003FF003E107DE1003A580000000034080074031000000008007403000000000008000800800002434D4430000000060000000100405748543000000006010052494D47
    ackLoopInClientRole();
    LOGI(TAG, "<< %s (expecting RIMG)", getCmdName().c_str());
    // RIMG / +0x20 / 52 bytes extra data
    cmdSpan = std::span(readBuffer).subspan(0, 44);
    constexpr uint8_t RIMG_EXTRA_DATA[]{0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x08, 0x00, 0x06, 0x00, 0x02, 0x07, 0x08,
                                        0x00, 0x06, 0x00, 0x06, 0x40, 0x04, 0xB0, 0x05, 0x00, 0x03, 0xC0, 0x04, 0x00,
                                        0x03, 0x00, 0x03, 0x20, 0x02, 0x58, 0x02, 0x80, 0x01, 0xE0, 0x01, 0x40, 0x00,
                                        0xF0, 0x03, 0xC4, 0x20, 0x04, 0x01, 0xC4, 0x20, 0x05, 0x00, 0x00, 0x18, 0x00};
    response = cmdToResponse(cmdSpan, 0x20, RIMG_EXTRA_DATA);
    LOGI(TAG, ">> BDY0");
    writeFrame(ourPort, makeseq(SEQ_DATA, true, true), response);

    // < 0xBE 0x090301
    ackLoopInClientRole();
    if (!expectStartsWith({session, 0x03, 0x01})) return false;
    LOGD(TAG, "i++ from watch");
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));

    // RINF
    // < 0xD4
    // 0x09030000002003FF003E107DE1003A580000000034080074031000000008007403000000000008000800810002434D4430000000060000000100405748543000000006010052494E46
    ackLoopInClientRole();
    LOGI(TAG, "<< %s (expecting RINF)", getCmdName().c_str());
    // RINF / -0x0C / 8 bytes extra data
    cmdSpan = std::span(readBuffer).subspan(0, 44);
    constexpr uint8_t RINF_EXTRA_DATA[]{0x00, 0x00, 0x10, 0xFF, 0xFF, 0x11, 0xFF, 0xFF};
    response = cmdToResponse(cmdSpan, -0x0C, RINF_EXTRA_DATA);
    LOGI(TAG, ">> BDY0");
    writeFrame(ourPort, makeseq(SEQ_DATA, true, true), response);

    // < 0xBE 0x090301
    ackLoopInClientRole();
    if (!expectStartsWith({session, 0x03, 0x01})) return false;
    LOGD(TAG, "i++ from watch");
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));

    // RCMD
    // < 0xF8
    // 0x09030000002003FF003E107DE1003A580000000034080074031000000008007403000000000008000800820002434D4430000000060000000100405748543000000006010052434D44
    ackLoopInClientRole();
    LOGI(TAG, "<< %s (expecting RCMD)", getCmdName().c_str());
    // RCMD / -0x0D / 7 bytes extra data
    cmdSpan = std::span(readBuffer).subspan(0, 44);
    // NOTE i removed an extra unaccounted for 0xc4 off the end of this.
    constexpr uint8_t RCMD_EXTRA_DATA[]{0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x01};
    response = cmdToResponse(cmdSpan, -0x0D, RCMD_EXTRA_DATA);
    LOGI(TAG, ">> BDY0");
    writeFrame(ourPort, makeseq(SEQ_DATA, true, true), response);

    // < 0xBE 0x090301
    ackLoopInClientRole();
    if (!expectStartsWith({session, 0x03, 0x01})) return false;
    LOGD(TAG, "i++ from watch");
    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));

    for (size_t fileCount = 0; true; fileCount++) {
        // PSRamFS doesn't implement size() correctly last i checked, we can track
        size_t fileSize = 0;

        ackLoopInClientRole();
        if (expectStartsWith({session, 0x03, 0x00, 0x00, 0x00, 0x30})) {
            LOGI(TAG, "Nothing more to receive! We're done.");
            writeFrame(ourPort, makeseq(SEQ_ACK, true, false));
            break;
        } else if (expectStartsWith({session, 0x03, 0x00, 0x00, 0x00, 0x20})) {
            LOGD(TAG, "READY FOR NEXT FILE!");
        } else {
            LOGE(TAG, "Unexpected command");
            return false;
        }

        // TODO this is actually a chunk - move into main loop
        // Really, I think all bytes are included in the .UPF file, and so the .JPG file exists within the .UPF file
        // NOTE it might have a bit of a special header
        // FIL0
        std::string fileCmdName = std::string(reinterpret_cast<const char *>(readBuffer + 0x42), 4);
        std::string fileName = std::string(reinterpret_cast<const char *>(readBuffer + 0x4C), 12);
        LOGI(TAG, "<< %s saving to %s", fileCmdName.c_str(), fileName.c_str());
        File file = PSRamFS.open(("/" + fileName).c_str(), "w", true);
        // We'll use this later in the RDY0 response
        uint8_t cmdFil0Seq = readBuffer[0x31];
        writeFrame(ourPort, makeseq(SEQ_ACK, true, false));

        readFrame();
        if (!expectStartsWith({session, 0x03, 0x00, 0x00})) return false;

        auto jpegSpan = Chunk::findJpegRegion(std::span(readBuffer, dataLen));
        if (jpegSpan.size() > 0) {
            LOGI(TAG, "Found beginning of JPEG inside chunk");
            file.write(jpegSpan.data(), jpegSpan.size());
            fileSize += jpegSpan.size();
        }
        writeFrame(ourPort, makeseq(SEQ_ACK, true, false));

        uint8_t SESSION_CHUNK_HEADER[]{session, 0x03, 0x00, 0x00};
        Chunk::Header header;

        do {  // LOOP OVER CHUNKS IN FILE

            size_t frameNum = 0;
            size_t chunkBytesReceived;

            do {  // LOOP OVER FRAMES IN CHUNK

                readFrame();
                if (!expectStartsWith(SESSION_CHUNK_HEADER)) {
                    LOGE(TAG, "Unexpected session header");
                    return false;
                }

                LOGD(TAG, "Chunk frame %d", frameNum);
                if (frameNum == 0) {
                    // CHUNK FIRST FRAME
                    auto maybeHeader = Chunk::parseHeader(std::span(readBuffer).subspan(4, dataLen - 4));
                    if (!maybeHeader.has_value()) {
                        LOGE(TAG, "Chunk header not found");
                        return false;
                    }

                    LOGD(TAG, "First frame, parsed chunk header");
                    header = maybeHeader.value();
                    chunkBytesReceived = 0;

                    Display::showProgressScreen(header.chunkNumber, header.chunkNumber + header.chunksLeft, fileCount);

                    LOGD(TAG, "Chunk %02d/%02d, %02d remain", header.chunkNumber,
                         header.chunkNumber + header.chunksLeft, header.chunksLeft);
                    size_t headersSize = sizeof(SESSION_CHUNK_HEADER) + Chunk::HEADER_SIZE;
                    size_t bytes = dataLen - headersSize;
                    chunkBytesReceived += bytes;
                    file.write(readBuffer + headersSize, bytes);
                    fileSize += bytes;

                    LOGD(TAG, "Received %d/%d bytes in chunk, expecting more frames? %s", chunkBytesReceived,
                         header.chunkSize, chunkBytesReceived < header.chunkSize ? "Yes" : "No");
                } else {
                    // CHUNK CONTINUATION FRAME
                    if (dataLen <= 4) {
                        LOGE(TAG, "Expected continuation frame, as %d bytes remain in this chunk",
                             header.chunkSize - chunkBytesReceived);
                        return false;
                    }

                    size_t headersSize = sizeof(SESSION_CHUNK_HEADER);
                    size_t bytes = dataLen - headersSize;
                    chunkBytesReceived += bytes;
                    file.write(readBuffer + headersSize, bytes);
                    fileSize += bytes;

                    LOGD(TAG, "Received %d/%d bytes in chunk, expecting more frames? %s", chunkBytesReceived,
                         header.chunkSize, chunkBytesReceived < header.chunkSize ? "Yes" : "No");
                }

                // Periodically send "Continue", otherwise normal ACK
                // arbitrary loop point
                if (seq % 0xf > 8) {
                    LOGD(TAG, ">> continue...");
                    uint8_t CONTINUE[]{0x03, session, 0x06};
                    writeFrame(ourPort, makeseq(SEQ_DATA, true, true), CONTINUE);
                } else {
                    writeFrame(ourPort, makeseq(SEQ_ACK, true, false));
                }

                frameNum++;
            } while (chunkBytesReceived < header.chunkSize);  // END LOOP OVER FRAMES IN CHUNK

            LOGI(TAG, "Chunk finished=%d chunks left=%d", header.isFinalChunk, header.chunksLeft);

        } while (!header.isFinalChunk);  // END LOOP OVER CHUNKS IN FILE

        LOGI(TAG, "File '%s' done!", fileName.c_str());
        file.close();

        Image::postProcess(fileName, fileSize);

        // RPL0
        if (!(readFrame() && expectAck())) return false;
        uint8_t RPL0_CMD[]{0x03, session, 0x00, 0x00, 0x00, 0x20, 0x03, 0xFF, 0x00, 0x38, 0x10, 0xC1, 0x00, 0x34,
                           0x58, 0x40,    0x00, 0x00, 0x00, 0x2E, 0x08, 0x00, 0x74, 0x03, 0x00, 0x00, 0x00, 0x00,
                           0x08, 0x00,    0x74, 0x03, 0x10, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, cmdFil0Seq,
                           0x00, 0x01,    0x52, 0x50, 0x4C, 0x30, 0x00, 0x00, 0x00, 0x0E, 0x01, 0x00, 0x00, 0x00,
                           0x00, 0x00,    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        std::memcpy(RPL0_CMD + 0x36, fileName.data(), 12);
        writeFrame(ourPort, makeseq(SEQ_DATA, false, true), RPL0_CMD);

        ackLoopInClientRole();
        if (expectStartsWith({session, 0x03, 0x01})) {
            LOGD(TAG, "i++ from watch");
            writeFrame(ourPort, makeseq(SEQ_ACK, true, false));
        } else {
            return false;
        }
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
        for (int i = 0; i < 5; i++) {
            ping();
            delay(100);
        }

        if (swapRolesAndCloseSession() && openSessionInClientRole() && syncInClientRole()) {
            // Eventually we'll get hung up on, use a low timeout
            ackLoopInClientRole(250);
            Display::showMountedScreen();
            delay(500);
            MassStorage::begin();
            return;
        }

        delay(10000);
    } else {
        delay(1000);
    }

    // LOGE(TAG, "Failure or no watch present, restarting from handshake");
    // delay(1000);
}
