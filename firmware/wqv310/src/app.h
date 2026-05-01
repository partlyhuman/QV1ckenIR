#pragma once

#include <Arduino.h>

#include <span>
#include <string>
#include <utility>

// Presentation/session layer
// The first 4+ bytes of a frame are a session header
// The first byte of this differentiates commands into
// several categories
namespace App {

constexpr uint8_t IRDA_STACK[]{0x19, 0x36, 0x66, 0xBE};
constexpr uint8_t IRDA_META[]{0x01, 0x19, 0x36, 0x66, 0xBE};
constexpr uint8_t IRDA_IDENT[]{0x01, 0x19, 0x36, 0x66, 0xBE, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0xFF};

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

// Negotiate in client mode
constexpr uint8_t CLIENT_SESSION_BEGIN[]{0x80, 0x01, 0x01, 0x00};
constexpr uint8_t CLIENT_REPLY_SESSION_BEGIN[]{0x81, 0x00, 0x81, 0x00};
// constexpr uint8_t CLIENT_SESSIONID_ASSIGN[]{SESH, 0x03, 0x01, 0x00, 0x01};
constexpr uint8_t CLIENT_REPLY_SESSIONID_ASSIGN[]{0x83, SESH, 0x81, 0x00, 0x0e};

// Receive in client mode
constexpr uint8_t CLIENT_APP_ITER_NEXT[]{SESH, 0x03, 0x01};
constexpr uint8_t CLIENT_APP_PACKET[]{SESH, 0x03, 0x00, 0x00};
constexpr uint8_t CLIENT_APP_FILES_NEXT[]{SESH, 0x03, 0x00, 0x00, 0x00, 0x20};
constexpr uint8_t CLIENT_APP_FILES_DONE[]{SESH, 0x03, 0x00, 0x00, 0x00, 0x30};

// Send in client mode
constexpr uint8_t CLIENT_REPLY_IDENT[]{0x03, SESH, 0x00, 0x00, 0x00, 0x11};
constexpr uint8_t CLIENT_REPLY_HANGUP[]{0x03, SESH, 0x06};
constexpr uint8_t CLIENT_REPLY_APP_PACKET[]{0x03, SESH, 0x00, 0x00};

// For any session-bound data, fill in the SESH blanks with the actual session
std::span<const uint8_t> fill(std::span<const uint8_t> cmd, uint8_t session);

// Mimics opaque transformations done on client to respond affirmatively to app commands
std::vector<uint8_t> makeResponse(std::span<const uint8_t> src, uint8_t session, int8_t shifts,
                                  std::span<const uint8_t> extraData = {});

// Given a FIL0 command, returns the filename and a RPL0 response
std::pair<std::string, std::vector<uint8_t>> makeFilRplResponse(std::span<const uint8_t> src, uint8_t session);

// For app payloads, get the 4-char command string at the end
std::string getCmdName(std::span<const uint8_t> src);

// Strip off the session layer header (CLIENT_APP_PACKET)
std::span<const uint8_t> getAppPacketPayload(std::span<const uint8_t> readBuffer);

}  // namespace App