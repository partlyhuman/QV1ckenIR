#pragma once

#include <Arduino.h>

#include <span>

// Presentation/session layer
// The first 4+ bytes of a frame are a session header
// The first byte of this differentiates commands into
// several categories
namespace WQV3 {
constexpr uint8_t IRDA_META[]{0x01, 0x19, 0x36, 0x66, 0xBE};

// Pick something that is absolutely illegal to appear otherwise
constexpr uint8_t SESH{0xC1};

constexpr uint8_t SESSION_BEGIN[]{0x80, 0x03, 0x01, 0x00};
constexpr uint8_t SESSION_NEGOTIATE[]{0x80, 0x03, 0x02, 0x01};
constexpr uint8_t SESSION_IDENT[]{0x83, SESH, 0x01, 0x00, 0x0E};
constexpr uint8_t SESSION_END[]{0x83, SESH, 0x02, 0x01};

constexpr uint8_t CMD_SWAP_ROLES[]{0x03, SESH, 0x00, 0x00, 0x01};
constexpr uint8_t CMD_PAGE_FWD[]{0x03, SESH, 0x00, 0x00, 0x02};
constexpr uint8_t CMD_PAGE_BACK[]{0x03, SESH, 0x00, 0x00, 0x03};
// ...? 4 5 6?
constexpr uint8_t CMD_SET_TIME[]{0x03, SESH, 0x00, 0x00, 0x07};

// Receive in client mode
constexpr uint8_t CLIENT_APP_PACKET[]{SESH, 0x03, 0x00, 0x00};
constexpr uint8_t CLIENT_APP_ITER_NEXT[]{SESH, 0x03, 0x01};

// Send in client mode
constexpr uint8_t CLIENT_REPLY_IDENT[]{0x03, SESH, 0x00, 0x00, 0x00, 0x11};
constexpr uint8_t CLIENT_REPLY_HANGUP[]{0x03, SESH, 0x06};
constexpr uint8_t CLIENT_REPLY_APP_PACKET[]{0x03, SESH, 0x00, 0x00};

// TODO
// std::span<const uint8_t> fill(uint8_t cmd[], uint8_t session);
// TODO move from main
// std::vector<uint8_t> cmdToResponse(std::span<const uint8_t> src, int8_t shifts,
//                                    std::span<const uint8_t> extraData = {});

}  // namespace WQV3