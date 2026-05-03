#include <FFat.h>

#include <cstring>

#include "app.h"
#include "chunk.h"
#include "config.h"
#include "display.h"
#include "firmware.h"
#include "frame.h"
#include "image.h"
#include "irda_hal.h"
#include "log.h"
#include "msc.h"
#include "stl_helpers.h"
#include "types.h"

// Shortcut to fill in the arg with the current session
// e.g. S(CMD_SET_TIME)
#define S(arg) App::fill((arg), session)
// Temp debugging aid
#define LOGFAIL            \
    {                      \
        LOGE(TAG, "FAIL"); \
        Frame::log(frame); \
        return false;      \
    }

using namespace App;

static const char *TAG = "Main";

static const size_t ABORT_AFTER_RETRIES = 10;
static uint8_t ourPort = 0xff;
static uint8_t watchPort = 0xff;
static uint8_t session;
static int model;
static Frame::Frame frame;

enum SequenceType {
    SEQ_ACK,
    SEQ_DATA,
};

static uint8_t seq_upper = 1, seq_lower = 0;

inline uint8_t seq(SequenceType type = SEQ_ACK, bool incUpper = false, bool incLower = false) {
    if (incUpper) seq_upper += 0x2;
    if (incLower) seq_lower += 0x2;
    return (seq_upper & 0xf) << 4 | ((type == SEQ_ACK ? 1 : seq_lower) & 0xf);
}

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

    Image::init();
    MassStorage::init();

    // Button manually toggles between Sync/USB (IR/MSC) mode or reboots into other firmware
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

    digitalWrite(PIN_LED, LED_OFF);
    LOGI(TAG, "Setup complete");
    delay(2500);
    buttonPressed = false;  // ignore any button presses during boot
}

/**
 * Small helper to validate that a received frame has the expected properties (the port & control fields).
 * Checking the length of the data is optional. Checking the contents of the data is out of scope.
 */
static bool expect(uint8_t expectedport, uint8_t expectedseq, int expectedMinLength = -1) {
    if (frame.error != Frame::FRAME_OK) {
        LOGW(TAG, "Frame not okay, status=%d", frame.error);
        return false;
    }
    if (frame.port != expectedport) {
        LOGE(TAG, "Expected port=%02x got port=%02x\n", expectedport, frame.port);
        return false;
    }
    if (frame.seq != expectedseq) {
        LOGE(TAG, "Expected seq=%02x got seq=%02x\n", expectedseq, frame.seq);
        return false;
    }
    if (expectedMinLength >= 0 && frame.data.size() < expectedMinLength) {
        LOGE(TAG, "Expected at least %d bytes of data, got %d\n", expectedMinLength, frame.data.size());
        return false;
    }
    return true;
}

static bool expectAck() {
    if (frame.error != Frame::FRAME_OK) {
        LOGE(TAG, "Frame not okay, status=%d", frame.error);
        return false;
    }
    if (frame.port != watchPort) {
        LOGE(TAG, "Expected port=%02x got port=%02x\n", watchPort, frame.port);
        return false;
    }
    if ((frame.seq & 0xF) != 1) {
        LOGE(TAG, "Expected seq=X1 (ACK) got seq=%02x\n", frame.seq);
        return false;
    }
    if (frame.data.size() > 0) {
        LOGE(TAG, "Expected empty data, got %d\n", frame.data.size());
        return false;
    }
    return true;
}

template <size_t N>
static bool expectStartsWith(const uint8_t (&cmp)[N]) {
    if (frame.error != Frame::FRAME_OK) LOGFAIL;
    bool ret = frame.data.size() >= N && std::equal(frame.data.begin(), frame.data.begin() + N, cmp);
    if (!ret) {
        LOGE(TAG, "Expected frame to start with:");
        for (auto b : cmp) Serial.printf("%02x ", b);
        Serial.println();
    }
    return ret;
}

static bool expectStartsWith(std::span<const uint8_t> cmp) {
    if (frame.error != Frame::FRAME_OK) LOGFAIL;
    bool ret = frame.data.size() >= cmp.size() && std::equal(cmp.begin(), cmp.end(), frame.data.begin());
    if (!ret) {
        LOGE(TAG, "Expected frame to start with:");
        for (auto b : cmp) Serial.printf("%02x ", b);
        Serial.println();
    }
    return ret;
}

static bool expectExactly(std::span<const uint8_t> cmp) {
    if (frame.error != Frame::FRAME_OK) LOGFAIL;
    bool ret = frame.data.size() == cmp.size() && std::equal(cmp.begin(), cmp.end(), frame.data.begin());
    if (!ret) {
        LOGE(TAG, "Expected frame to start with:");
        for (auto b : cmp) Serial.printf("%02x ", b);
        Serial.println();
    }
    return ret;
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
        Frame::writeFrame(0xff, 0x3f, send, 11);
        frame = Frame::readFrame(25);
        if (expect(0xfe, 0xbf, 1)) break;
    }

    // Did we get any response from that?
    if (frame.error) return false;

    // What watch is connecting?
    // 01193666 BED60300 00010500 84040043 4153494F 20574943 20323431 322F4952
    //                                     C A S I  O   W I  C 2 4 1  2 / I R
    // 01193666 BE0E0300 00010500 8C040043 4153494F 20574943 20323431 312F4952
    //                                     C A S I  O   W I  C 2 4 1  1 / I R

    auto watchIdString = Frame::extractString(frame, 15, 17);

    if (watchIdString == "CASIO WIC 2411/IR") {
        model = 3;
        Display::setModel(model);
    } else if (watchIdString == "CASIO WIC 2412/IR") {
        model = 10;
        Display::setModel(model);
    } else {
        LOGE(TAG, "Unrecognized watch");
        return false;
    }
    Display::showConnectingScreen("Negotiating session...");

    std::array<uint8_t, 27> START_SESSION{0x19, 0x36, 0x66, 0xBE, watchPort, 0x01, 0x01, 0x02, 0x82,
                                          0x01, 0x01, 0x83, 0x01, 0x3F,      0x84, 0x01, 0x0F, 0x85,
                                          0x01, 0x80, 0x86, 0x02, 0x80,      0x03, 0x08, 0x01, 0x07};

    send = concat(IRDA_RAND_STR, START_SESSION);
    Frame::writeFrame(0xff, 0x93, send, 5);
    frame = Frame::readFrame();
    if (frame.error) LOGFAIL;
    LOGI(TAG, "Watch accepted SIP port %02x", watchPort);

    // Now we initialize the sequence state machine
    seq_upper = 1;
    seq_lower = 0;

    Frame::writeFrame(ourPort, seq(SEQ_DATA, false, false), S(SESSION_BEGIN));
    // Expect an 83 back
    frame = Frame::readFrame();
    if (frame.error) LOGFAIL;

    // 0x80030201 -- let's negotiate a session?
    Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), S(SESSION_NEGOTIATE));
    // Expect ACK
    frame = Frame::readFrame();
    if (!expectAck()) LOGFAIL;

    // Generate session ID! Needs to be >3 <=F
    session = random(0x4, 0xf);

    // 0x830401000E -- assign session 04
    Frame::writeFrame(ourPort, seq(SEQ_DATA, false, true), S(SESSION_IDENT));
    // Expect  0x8403810001 (first byte = 0x80 | session)
    frame = Frame::readFrame();
    if (frame.error || frame.data.size() < 2) LOGFAIL;

    LOGI(TAG, "Confirmed session id %02x by %02x", session, frame.data[1]);

    // > 0xB1
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));
    // expect ack
    frame = Frame::readFrame();
    // if (frame.error) LOGFAIL

    // TODO we can't reverse roles after going into this state?
    // // Is 0x7d sometimes there and sometimes not??? // 0x7d is escape so this can't be right without a subsequent
    // byte const uint8_t SESSION_INIT_END[]{0x80, 0x03, 0x01, 0x00, 0x7D}; Frame::writeFrame(ourPort, seq(SEQ_DATA,
    // true, true), SESSION_INIT_END); if (!readFrame()) return false;

    return true;
}

Frame::Frame readAckUntilDataFrame(unsigned long timeout = 2000) {
    for (int i = 0; true; i++) {
        if (i > ABORT_AFTER_RETRIES) {
            return Frame::errorFrame(Frame::FRAME_TIMEOUT);
        }

        frame = Frame::readFrame();

        if (frame.error) {
            // Ask for a resend
            Frame::writeFrame(ourPort, seq());
            continue;
        }

        if (frame.seq == 0x53) {
            // TODO signal back up that we need to retry sending the payload
            LOGE(TAG, "Error status");
            return Frame::errorFrame(Frame::FRAME_APP_ERROR);
        }

        if (frame.port == watchPort) {
            if (frame.data.size() == 0 && (frame.seq & 0xf) == 1) {
                // ACK, we'll reply ACK
                Frame::writeFrame(ourPort, seq());
            } else if (frame.data.size() > 0) {
                // Let the caller resume with the data still in place
                return frame;
            } else {
                LOGE(TAG, "Unexpected frame in ack loop");
                Frame::log(frame);
                Frame::writeFrame(ourPort, seq());
            }
        }
    }
}

bool openSessionInClientRole() {
    watchPort = 0xff;
    ourPort = 0xfe;
    seq_lower = 0;
    seq_upper = 1;

    Display::showConnectingScreen("Switching roles");

    // < 0x3F 0x01193666BEFFFFFFFF010000...
    // give first one a long time to arrive
    frame = Frame::readFrame(5000);
    if (frame.error == Frame::FRAME_TIMEOUT) {
        LOGE(TAG, "Was waiting for watch to take over connection, but timed out waiting.");
        return false;
    }

    // > 0xBF 0x01(0E030000)193666BE01050084250057494E444F5753585000 "WINDOWS XP"
    // We replace that with QV1-BLINK
    uint8_t IRDA_HI[]{0x01, 0,    0,    0,    0,    0x19, 0x36, 0x66, 0xbe, 0x01, 0x05, 0x00, 0x84,
                      0x25, 0x00, 0x51, 0x56, 0x31, 0x2d, 0x42, 0x4c, 0x49, 0x4e, 0x4b, 0x00};
    for (int i = 1; i <= 4; i++) {
        IRDA_HI[i] = esp_random() & ~0b11000001;
    }
    Frame::writeFrame(ourPort, 0xbf, IRDA_HI);

    do {
        // < 0x3F 0x01193666BEFFFFFFFF010100
        // < 0x3F 0x01193666BEFFFFFFFF010200
        // < 0x3F 0x01193666BEFFFFFFFF010300
        // < 0x3F 0x01193666BEFFFFFFFF010400
        // < 0x3F 0x01193666BEFFFFFFFF010500
        frame = Frame::readFrame();

        // < 0x3F 0x01193666BEFFFFFFFF01FF008C0400434153494F2057494320323431312F4952 ("casio wic...")
    } while (!frame.error && !expectStartsWith(IRDA_IDENT));

    auto watchIdentString = Frame::extractString(frame, 15, 17);
    LOGI(TAG, "IRDA stack host ident '%s'", watchIdentString.c_str());

    // < 0x93 0x193666BE0E030000(70)01013F82010183013F8401018501808601030801FF
    frame = Frame::readFrame();
    if (expectStartsWith(IRDA_STACK)) {
        ourPort = frame.data[8];
        watchPort = ourPort + 1;
        LOGI(TAG, "Accepting ports assigned by watch: device=%02x watch=%02x", ourPort, watchPort);
    } else {
        return false;
    }
    // Display::showConnectingScreen("Watch establishing session");

    // > 0x73 0x0E030000193666BE01010282010183013F84010F85018086028003080104
    constexpr uint8_t CONNECT_STR[]{0x0e, 0x03, 0x00, 0x00, 0x19, 0x36, 0x66, 0xbe, 0x01, 0x01,
                                    0x02, 0x82, 0x01, 0x01, 0x83, 0x01, 0x3f, 0x84, 0x01, 0x0f,
                                    0x85, 0x01, 0x80, 0x86, 0x02, 0x80, 0x03, 0x08, 0x01, 0x04};
    Frame::writeFrame(ourPort, 0x73, CONNECT_STR);

    readAckUntilDataFrame();
    // < 0x10 0x80010100
    if (!expectStartsWith(CLIENT_SESSION_BEGIN)) LOGFAIL;
    // > 0x30 0x81008100
    Frame::writeFrame(ourPort, seq(SEQ_DATA, true, false), CLIENT_REPLY_SESSION_BEGIN);

    // NOTE: the problem here turned out to be us replying so quickly the watch missed it when no Serial taking up time
    // Fixed this with repeating the BOF here, but maybe we should default to a higher default BOF repeat?
    int irdaFramesRecvd = 0;
    for (int i = 0; true; i++) {
        if (i > ABORT_AFTER_RETRIES) LOGFAIL;
        frame = Frame::readFrame();
        if (frame.error) LOGFAIL;

        if (frame.data.size() == 5 && frame.data[0] > 0x80 && frame.data[0] <= 0x8F) {
            session = (frame.data[0] & 0xf);
            Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), S(CLIENT_REPLY_SESSIONID_ASSIGN));
            break;
        } else if (expectStartsWith({0x00, 0x01, 0x84})) {
            if (irdaFramesRecvd == 0) {
                constexpr uint8_t IRDA_NEGOTIATE[]{0x01, 0x00, 0x84, 0x00, 0x00, 0x01, 0x00,
                                                   0x05, 0x01, 0x00, 0x00, 0x00, 0x09};
                Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), IRDA_NEGOTIATE);
            } else if (irdaFramesRecvd == 1) {
                constexpr uint8_t IRDA_NEGOTIATE[]{0x01, 0x00, 0x84, 0x00, 0x00, 0x01, 0x00, 0x05, 0x02,
                                                   0x00, 0x06, 0x00, 0x01, 0x06, 0x01, 0x01, 0x01};
                Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), IRDA_NEGOTIATE);
            } else {
                LOGE(TAG, "ran out of things to say");
                return false;
            }
            irdaFramesRecvd++;
        } else if ((frame.seq & 0xf) == 1) {
            Frame::writeFrame(ourPort, seq(SEQ_ACK, false, false));
        } else {
            LOGFAIL;
        }
    }

    // // Lots of IRDA garbage who cares
    // // TODO see if we can just ACK
    // // < 0x32 0x0001840B497244413A4972434F4D4D13497244413A54696E7954503A4C73617053656C
    // frame = Frame::readFrame();
    // // if (dataLen > 0 && readBuffer[0] == 0 && readBuffer[1] == 0x01) {
    // constexpr uint8_t IRDA_NEGOTIATE_0[]{0x01, 0x00, 0x84, 0x00, 0x00, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x00,
    // 0x09};
    // // > 0x52 0x01008400000100050100000009
    // Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), IRDA_NEGOTIATE_0);

    // // more IRDA garbage
    // // TODO see if we can just ACK
    // // < 0x54 0x0001840B497244413A4972434F4D4D0A506172616D65746572737D
    // frame = Frame::readFrame();
    // // > 0x74 0x0100840000010005020006000106010101
    // constexpr uint8_t IRDA_NEGOTIATE_1[]{0x01, 0x00, 0x84, 0x00, 0x00, 0x01, 0x00, 0x05, 0x02,
    //                                      0x00, 0x06, 0x00, 0x01, 0x06, 0x01, 0x01, 0x01};
    // Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), IRDA_NEGOTIATE_1);

    // < 0x76 0x8903010001
    // LOGE(TAG, "1");
    // frame = readAckUntilDataFrame();
    // LOGE(TAG, "Expecting Frame: 0x8?03010001, got:");
    // Frame::log(frame);
    // // Don't allow 0 as session
    // if (!frame.error && frame.data.size() > 0 && frame.data[0] > 0x80 && frame.data[0] <= 0x8f) {
    //     LOGE(TAG, "2");
    //     session = (frame.data[0] & 0xf);
    //     LOGI(TAG, "Received session id %01x from watch", session);
    // } else {
    //     LOGE(TAG, "Missed session id");
    //     return false;
    // }
    //     LOGE(TAG, "3");

    // < 0x98 0x09030003000104
    frame = Frame::readFrame();
    if (!expectStartsWith({session, 0x03, 0x00, 0x03})) LOGFAIL;
    // > 0xB1
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));

    // < 0x9A 0x0903000C10040000258011010320010C
    frame = Frame::readFrame();
    if (!expectStartsWith({session, 0x03, 0x00, 0x0C})) LOGFAIL;
    // > 0xD1
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));

    LOGD(TAG, "Done opening session in client role");
    return true;
}

bool ping() {
    Frame::writeFrame(ourPort, seq(SEQ_ACK));
    frame = Frame::readFrame();
    return expectAck();
}

bool closeSession() {
    // Display::showConnectingScreen("Closing session as host");

    // 0x83SS0201
    Frame::writeFrame(ourPort, seq(SEQ_DATA, false, true), S(SESSION_END));
    Frame::readFrame();

    Frame::writeFrame(ourPort, seq(SEQ_ACK, false, true));
    Frame::readFrame();

    Frame::writeFrame(ourPort, 0x53);
    Frame::readFrame();
    // replies with 73
    // Frame::writeFrame(ourPort, 0x73);
    return true;
}

bool swapRolesAndCloseSession() {
    // Display::showConnectingScreen("Requesting watch takes over");

    // > 0x0308000001
    if (model == 3) {
        Frame::writeFrame(ourPort, seq(SEQ_DATA, false, true), S(CMD_SWAP_ROLES_3));
    } else if (model == 10) {
        Frame::writeFrame(ourPort, seq(SEQ_DATA, false, true), S(CMD_SWAP_ROLES_10));
    } else {
        return false;
    }
    // < 0x0803010C100400002580110103000104
    Frame::readFrame();

    // > 0xD1
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));
    // < 0x080300000D
    Frame::readFrame();

    // > 0xF1
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));
    Frame::readFrame();

    closeSession();
    return true;
}

void page(int pageDir) {
    if (pageDir == 0) return;
    auto cmd = S(pageDir > 0 ? CMD_PAGE_FWD : CMD_PAGE_BACK);
    Frame::writeFrame(ourPort, seq(SEQ_DATA, false, true), cmd);
    Frame::readFrame();
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));
    Frame::readFrame();
}

void sendTime(Timestamp time) {
    auto cmd = S(CMD_SET_TIME);
    std::vector<uint8_t> sendBuffer(std::size(cmd) + sizeof(Timestamp));
    sendBuffer.insert(sendBuffer.end(), cmd.begin(), cmd.end());
    std::memcpy(sendBuffer.data() + sendBuffer.size(), &time, sizeof(Timestamp));

    Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), sendBuffer);
    frame = Frame::readFrame();
}

bool syncInClientRole() {
    // NOTE it's important to only do work when the watch is not sending (waiting for our reply)
    frame = Frame::readFrame();

    Display::showConnectingScreen("Watch establishing sync");

    LOGI(TAG, "Formatting FatFS");
    FFat.end();
    FFat.format();
    FFat.begin();

    // Previously this was flash, then PSRAM, but largest files ~8kb, and we have minimum 250kb heap free
    static std::vector<uint8_t> fileBuffer;
    fileBuffer.clear();
    fileBuffer.reserve(8192);

    Image::init();

    Frame::writeFrame(ourPort, seq());

    readAckUntilDataFrame();
    // < 0x9C
    // 0x090300000010000101341004000000000000000008007403000000001166723A330D0A69643A434153494F2057494320323431312F495220202020200D0A10020000
    if (expectStartsWith({session, 0x03, 0x00, 0x00, 0x00, 0x10})) {
        // This one is null terminated
        auto watchFw = Frame::extractString(frame, 0x26, 0x16);
        LOGI(TAG, "Identified as id:'%s'", watchFw.c_str());
    } else {
        return false;
    }
    // TODO could we skip the ack/ack?
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));

    frame = Frame::readFrame();
    if (!expectAck()) LOGFAIL;
    uint8_t FW_IDENT[]{0x03, session, 0x00, 0x00, 0x00, 0x11, 0x01, 0x30, 0x10, 0x04, 0x08, 0x00, 0x74, 0x03,
                       0x00, 0x00,    0x00, 0x00, 0x08, 0x00, 0x74, 0x03, 0x10, 0x00, 0x00, 0x00, 0x11, 0x66,
                       0x72, 0x3A,    0x31, 0x0D, 0x0A, 0x69, 0x64, 0x3A, 0x4C, 0x49, 0x4E, 0x4B, 0x20, 0x51,
                       0x57, 0x32,    0x34, 0x31, 0x31, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x0D, 0x0A};
    Frame::writeFrame(ourPort, seq(SEQ_DATA, false, true), FW_IDENT);

    // < 0xBE 0x090301
    frame = readAckUntilDataFrame();
    if (!expectExactly(S(CLIENT_APP_ITER_NEXT))) LOGFAIL;
    LOGD(TAG, "<< Yield next object from watch");
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));

    // RIMG
    // < 0xB0
    // 0x09030000002003FF003E107DE1003A580000000034080074031000000008007403000000000008000800800002434D4430000000060000000100405748543000000006010052494D47
    frame = readAckUntilDataFrame();
    if (getCmdName(frame) != "RIMG") {
        LOGE(TAG, "Wrong command, expecting RIMG");
        return false;
    }
    LOGD(TAG, "<< RIMG");
    LOGD(TAG, ">> BDY0");
    Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), makeResponse(frame));

    // < 0xBE 0x090301
    frame = readAckUntilDataFrame();
    if (!expectStartsWith({session, 0x03, 0x01})) LOGFAIL;
    LOGD(TAG, "i++ from watch");
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));

    // RINF
    // < 0xD4
    // 0x09030000002003FF003E107DE1003A580000000034080074031000000008007403000000000008000800810002434D4430000000060000000100405748543000000006010052494E46
    frame = readAckUntilDataFrame();
    if (getCmdName(frame) != "RINF") {
        LOGE(TAG, "Wrong command, expecting RINF");
        return false;
    }
    LOGD(TAG, "<< RINF");
    LOGD(TAG, ">> BDY0");
    Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), makeResponse(frame));

    // < 0xBE 0x090301
    frame = readAckUntilDataFrame();
    if (!expectStartsWith({session, 0x03, 0x01})) LOGFAIL;
    LOGD(TAG, "i++ from watch");
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));

    // RCMD
    // < 0xF8
    // 0x09030000002003FF003E107DE1003A580000000034080074031000000008007403000000000008000800820002434D4430000000060000000100405748543000000006010052434D44
    frame = readAckUntilDataFrame();
    if (getCmdName(frame) != "RCMD") {
        LOGE(TAG, "Wrong command, expecting RCMD");
        return false;
    }
    LOGD(TAG, "<< RCMD");
    LOGD(TAG, ">> BDY0");
    Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), makeResponse(frame));

    // < 0xBE 0x090301
    frame = readAckUntilDataFrame();
    if (!expectExactly(S(CLIENT_APP_ITER_NEXT))) LOGFAIL;
    LOGD(TAG, "i++ from watch");
    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));

    for (size_t fileCount = 0; true; fileCount++) {
        frame = readAckUntilDataFrame();
        if (expectStartsWith(S(CLIENT_APP_FILES_NEXT))) {
            LOGD(TAG, "READY FOR NEXT FILE!");
        } else if (expectStartsWith(S(CLIENT_APP_FILES_DONE))) {
            LOGI(TAG, "Nothing more to receive! We're done.");
            Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));
            break;
        } else {
            LOGE(TAG, "Unexpected command");
            return false;
        }

        // TODO this is actually a chunk - move into main loop
        // As is, this makes the assumption 2 frames/chunk, no good!
        // Really, I think all bytes are included in the .UPF file, and so the .JPG
        // file exists within the .UPF file NOTE it might have a bit of a special
        // chunk header

        // FIL0
        // We have to send the RDY0 much later, so let's create it now instead of holding onto the FIL0
        auto [fileName, rpl0] = makeFilRplResponse(frame);
        auto fileCmdName = Frame::extractString(frame, 0x42, 4);
        LOGI(TAG, "<< %s saving to %s", fileCmdName.c_str(), fileName.c_str());
        Display::showProgressScreen(0, fileCount);
        Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));

        frame = Frame::readFrame();
        if (!expectStartsWith(S(CLIENT_APP_PACKET))) return false;

        // TODO Don't make the assumption that the JPEG data only comes in the second frame
        auto jpegSpan = Chunk::findJpegRegion(frame.data);
        if (jpegSpan.size() > 0) {
            LOGI(TAG, "Found beginning of JPEG inside chunk");
            fileBuffer.insert(fileBuffer.end(), jpegSpan.begin(), jpegSpan.end());
        }
        Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));

        Chunk::Header header;

        do {  // LOOP OVER CHUNKS IN FILE

            size_t frameNum = 0;
            size_t chunkBytesReceived = 0;

            do {  // LOOP OVER FRAMES IN CHUNK

                frame = Frame::readFrame();
                if (!expectStartsWith(S(CLIENT_APP_PACKET))) {
                    LOGE(TAG, "Unexpected session header");
                    return false;
                }
                // Snip off the session header - this is all app layer now
                auto payload = getAppPayload(frame);

                LOGD(TAG, "Chunk frame %d", frameNum);
                if (frameNum == 0) {
                    // CHUNK FIRST FRAME
                    auto maybeHeader = Chunk::parseHeader(payload);
                    if (!maybeHeader) {
                        LOGE(TAG, "Chunk header not found");
                        return false;
                    }
                    header = *maybeHeader;

                    LOGV(TAG, "Chunk %02d/%02d, %02d remain", header.chunkNumber,
                         header.chunkNumber + header.chunksLeft, header.chunksLeft);

                    auto chunkData = payload.subspan(Chunk::HEADER_SIZE);
                    chunkBytesReceived += chunkData.size();
                    fileBuffer.insert(fileBuffer.end(), chunkData.begin(), chunkData.end());

                } else {
                    // CHUNK CONTINUATION FRAME

                    auto chunkData = payload;
                    chunkBytesReceived += chunkData.size();
                    fileBuffer.insert(fileBuffer.end(), chunkData.begin(), chunkData.end());
                }

                Display::showProgressScreen(
                    (header.chunkNumber - 1.0f + ((float)chunkBytesReceived / header.chunkSize)) /
                        (header.chunkNumber + header.chunksLeft),
                    fileCount);

                LOGV(TAG, "Received %d/%d bytes in chunk, expecting more frames? %s", chunkBytesReceived,
                     header.chunkSize, chunkBytesReceived < header.chunkSize ? "Yes" : "No");

                // Periodically send "Continue", otherwise normal ACK
                // arbitrary loop point
                if (frame.seq % 0xf > 8) {
                    LOGV(TAG, ">> continue");
                    uint8_t CONTINUE[]{0x03, session, 0x06};
                    Frame::writeFrame(ourPort, seq(SEQ_DATA, true, true), CONTINUE);
                } else {
                    Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));
                }

                frameNum++;
            } while (chunkBytesReceived < header.chunkSize);  // END LOOP OVER FRAMES IN CHUNK

            LOGI(TAG, "Chunk finished=%d chunks left=%d", header.isFinalChunk, header.chunksLeft);

        } while (!header.isFinalChunk);  // END LOOP OVER CHUNKS IN FILE

        LOGI(TAG, "File '%s' done!", fileName.c_str());

        Display::showProgressScreen(1, fileCount);
        Image::postProcess(fileName, fileBuffer);
        fileBuffer.clear();

        // RPL0
        frame = Frame::readFrame();
        if (!expectAck()) LOGFAIL;
        // Use the RPL0 we created earlier
        Frame::writeFrame(ourPort, seq(SEQ_DATA, false, true), rpl0);

        frame = readAckUntilDataFrame();
        if (expectExactly(S(CLIENT_APP_ITER_NEXT))) {
            LOGD(TAG, "i++ from watch");
            Frame::writeFrame(ourPort, seq(SEQ_ACK, true, false));
        } else {
            return false;
        }
    }
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

    Display::showIdleScreen();
    if (openSession() && swapRolesAndCloseSession() && openSessionInClientRole() && syncInClientRole()) {
        // Eventually we'll get hung up on, use a low timeout
        readAckUntilDataFrame(250);
        Display::showMountedScreen();
        MassStorage::begin();
        return;
    }
}
