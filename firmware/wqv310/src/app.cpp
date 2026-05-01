#include "app.h"

#include <algorithm>
#include <cstring>

#include "log.h"

namespace App {

std::span<const uint8_t> fill(std::span<const uint8_t> cmd, uint8_t session) {
    static std::vector<uint8_t> CMD_BUFFER;
    CMD_BUFFER.reserve(64);
    std::replace_copy(cmd.begin(), cmd.end(), CMD_BUFFER.begin(), SESH, session);
    return std::span(CMD_BUFFER).subspan(0, cmd.size());
}

// TODO this expects the full frame, but conceptually we should strip off the session header and attach a session header
// in response
std::vector<uint8_t> makeResponse(std::span<const uint8_t> src, uint8_t session, int8_t shifts,
                                  std::span<const uint8_t> extraData) {
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

std::pair<std::string, std::vector<uint8_t>> makeFilRplResponse(std::span<const uint8_t> src, uint8_t session) {
    constexpr uint8_t RPL0_CMD[]{0x03, 0,    0x00, 0x00, 0x00, 0x20, 0x03, 0xFF, 0x00, 0x38, 0x10, 0xC1, 0x00, 0x34,
                                 0x58, 0x40, 0x00, 0x00, 0x00, 0x2E, 0x08, 0x00, 0x74, 0x03, 0x00, 0x00, 0x00, 0x00,
                                 0x08, 0x00, 0x74, 0x03, 0x10, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0,
                                 0x00, 0x01, 0x52, 0x50, 0x4C, 0x30, 0x00, 0x00, 0x00, 0x0E, 0x01, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    std::vector<uint8_t> reply(std::begin(RPL0_CMD), std::end(RPL0_CMD));
    reply[1] = session;
    reply[0x29] = src[0x31];
    auto fileNameSpan = src.subspan(0x4C, 12);
    std::memcpy(reply.data() + 0x36, fileNameSpan.data(), fileNameSpan.size());
    std::string fileName(reinterpret_cast<const char *>(fileNameSpan.data()), fileNameSpan.size());
    return std::pair(fileName, reply);
}

std::string getCmdName(std::span<const uint8_t> src) {
    const char *end = reinterpret_cast<const char *>(src.data() + src.size());
    return std::string(end - 4, 4);
}

std::span<const uint8_t> getAppPacketPayload(std::span<const uint8_t> readBuffer) {
    if (readBuffer.size() < std::size(CLIENT_APP_PACKET)) {
        return {};
    }
    // Could reject if doesn't start with session header
    return readBuffer.subspan(std::size(CLIENT_APP_PACKET));
}

}  // namespace App