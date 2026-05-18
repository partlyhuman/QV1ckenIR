#include "wqv_jpeg_core.h"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <format>
#include <span>
#include <stdexcept>

#include "MicroExif.h"
#include "log.h"

static const char *TAG = "JPEG";

time_t timestampToTime(const Timestamp src) {
    tm t{};
    t.tm_year = (2000 + src.year2k) - 1900;
    t.tm_mon = src.month - 1;
    t.tm_mday = src.day;
    t.tm_hour = src.hour;
    t.tm_min = src.minute;
    t.tm_isdst = 0;
    time_t time = mktime(&t);
    return time;
}

std::string trimTrailingSpaces(std::string src) {
    auto isTrim = [](unsigned char c) { return c == 0x20 || c == 0x00; };
    while (!src.empty() && isTrim((unsigned char)src.back())) {
        src.pop_back();
    }
    return src;
}

std::vector<uint8_t> makeExifBlob(const Timestamp &t, const std::string &title, int wqvModel) {
    ExifBuilder exif;

    const uint16_t TYPE_ASCII = 0x0002;

    // Manufacturer
    exif.addTag(ExifTag(0x010F, TYPE_ASCII, "Casio"));
    // Model
    auto model = std::format("WQV-{}", wqvModel);
    exif.addTag(ExifTag(0x0110, TYPE_ASCII, model.c_str()));

    // ModifyDate (DateTime)
    // YYYY:MM:DD HH:MM:SS
    auto dateTimeString =
        std::format("{:04}:{:02}:{:02} {:02}:{:02}:00", t.year2k + 2000, t.month, t.day, t.hour, t.minute);
    LOGV(TAG, "datetime = %s", dateTimeString.c_str());
    exif.addTag(ExifTag(0x0132, TYPE_ASCII, dateTimeString.c_str()));

    // ImageDescription
    if (title.size() > 0) {
        LOGV(TAG, "nonblank title '%s'", title.c_str());
        // Previously tried 0xa436 ImageTitle
        exif.addTag(ExifTag(0x010E, TYPE_ASCII, title.c_str()));
    }

    return exif.buildExifBlob();
}

std::pair<std::string, Timestamp> parseCasioJpegMetadata(std::vector<uint8_t> &data, bool replaceWithExif,
                                                         int wqvModel) {
    try {
        auto iter = data.begin();

        if (iter[0] != 0xff || iter[1] != 0xd8) throw std::runtime_error("Invalid SOI");
        iter += 2;

        // Make sure we at least have enough to read the marker
        while ((iter + 4) < data.end()) {
            if (iter[0] != 0xff) throw std::runtime_error("Expected 0xff in marker");
            uint8_t markerId = iter[1];
            iter += 2;

            // len includes the bytes themselves, but we don't want to consume them
            uint16_t len = (iter[0] << 8) | iter[1];
            LOGV(TAG, "Encountered marker %02x of length %d", marker, len);
            if (markerId != 0xe2) {
                iter += len;
                continue;
            }

            LOGD(TAG, "Found APP2 marker, extracting metadata");
            std::span<const uint8_t> marker(iter + 2, iter + len);
            if (marker.size() < sizeof(Timestamp)) throw std::runtime_error("APP2 marker not big enough for timestamp");

            std::string title;
            bool isAscii = std::all_of(marker.begin(), marker.end() - sizeof(Timestamp),
                                       [](unsigned char b) { return b <= 0x7F; });
            if (isAscii) {
                title = trimTrailingSpaces(
                    std::string(reinterpret_cast<const char *>(marker.data()), marker.size() - sizeof(Timestamp)));
            }

            Timestamp timestamp;
            std::memcpy(&timestamp, marker.data() + marker.size() - sizeof(Timestamp), sizeof(Timestamp));

            if (replaceWithExif) {
                data.erase(iter - 2, iter + len);
                auto exif = makeExifBlob(timestamp, title, wqvModel);
                data.insert(iter - 2, exif.begin(), exif.end());
            }

            return {title, timestamp};
        }
        LOGE(TAG, "Couldn't find APP2 marker in JPEG, no metadata");
    } catch (std::exception &e) {
        LOGE(TAG, "%s", e.what());
    }
    return {};
}
