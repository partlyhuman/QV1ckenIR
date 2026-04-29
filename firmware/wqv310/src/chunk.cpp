#include "chunk.h"

#include "log.h"

namespace Chunk {
static const char *TAG = "Chunk";

uint32_t readBigEndianUint32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | (uint32_t(p[3]));
}

uint16_t readBigEndianUint16(const uint8_t *p) {
    return (uint32_t(p[0]) << 8) | (uint32_t(p[1]));
}

std::optional<Header> parseHeader(std::span<const uint8_t> raw) {
    if (raw.size() < HEADER_SIZE) {
        LOGE(TAG, "Data shorter than chunk header size");
        return std::nullopt;
    }
    static constexpr uint8_t CHUNK_HEADER_MAGIC[]{0x00, 0x20, 0x03, 0xFF};
    if (!std::equal(raw.begin(), raw.begin() + 4, CHUNK_HEADER_MAGIC)) {
        LOGE(TAG, "Data does not look like a chunk header");
        return std::nullopt;
    }
    // 4 bytes:    00 20 03 FF
    // 4 bytes:    AA BB 10 CC (chunk header)
    //                      CC - 41 = initial, 01 = chunk, 81 = final
    //             AA BB - currently unknown purpose: observed 01 FA, 00 B8

    LOGV(TAG, "Unknown purpose bytes in header: %02x%02x", raw[5], raw[6]);

    Header h{};
    h.isFinalChunk = (raw[7] & 0x80) != 0;
    h.isInitialChunk = (raw[7] & 0x40) != 0;
    LOGD(TAG, "Chunk final/initial byte %02x", raw[7]);

    // 2 bytes:    NN NN (uint16 number of bytes that come after this in the chunk)
    //                   this is an application layer count so ignores all presentation/session layer

    h.chunkSize = readBigEndianUint16(raw.subspan(8).data());

    // 4 bytes:    00 00 UU UU (chunk number: counts up from 0 first chunk)
    // 4 bytes:    00 00 DD DD (chunk number: counts remaining down to 1 = last chunk)
    h.chunkNumber = readBigEndianUint32(raw.subspan(10).data());
    h.chunksLeft = readBigEndianUint32(raw.subspan(14).data()) - 1;

    return h;
}

std::span<const uint8_t> findJpegRegion(std::span<const uint8_t> raw) {
    constexpr uint8_t JPEG_MARKER[] = {0xFF, 0xD8, 0xFF, 0xE0};
    auto it = std::search(std::begin(raw), std::end(raw), std::begin(JPEG_MARKER), std::end(JPEG_MARKER));
    if (it == raw.end()) {
        LOGE(TAG, "Expected to find the JPEG start marker");
        return {};
    }
    return std::span<const uint8_t>(it, raw.end());
}

}  // namespace Chunk