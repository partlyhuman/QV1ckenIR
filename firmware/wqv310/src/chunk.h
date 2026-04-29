#pragma once
#include <Arduino.h>

#include <optional>
#include <span>
namespace Chunk {

// TODO i think the intial or final header can be shorter
constexpr size_t HEADER_SIZE = 18;

struct Header {
    bool isInitialChunk;
    bool isFinalChunk;
    uint16_t chunkSize;
    int32_t chunkNumber;
    int32_t chunksLeft;
};

std::optional<Header> parseHeader(std::span<const uint8_t> raw);

std::span<const uint8_t> findJpegRegion(std::span<const uint8_t> raw);

}  // namespace Chunk