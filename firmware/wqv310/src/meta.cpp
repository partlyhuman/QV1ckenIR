#include "meta.h"

#include <FFat.h>

#include <cstring>
#include <ctime>

#include "log.h"
namespace Meta {

const static char *TAG = "Meta";

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

void setSystemTime(const time_t time) {
    timeval epoch = {time, 0};
    settimeofday((const timeval *)&epoch, 0);
}

std::string trimTrailingSpaces(std::string src) {
    if (src.find_first_not_of(' ') == std::string::npos) return "";
    return src.substr(0, src.find_last_not_of(' ') + 1);
}

std::pair<std::string, Timestamp> getMetaFromJpegMarker(std::string path) {
    try {
        // Let's make things easy on ourselves and assume the marker is in the first 512 bytes of the file
        char headerBuffer[512];

        auto jpegFile = FFat.open(path.c_str(), "r");
        if (!jpegFile) throw std::runtime_error("Couldn't find file");
        size_t bytesRead = jpegFile.readBytes(headerBuffer, sizeof(headerBuffer));
        jpegFile.close();

        std::span header(headerBuffer, headerBuffer + bytesRead);
        auto iter = header.begin();

        if (iter[0] != 0xff || iter[1] != 0xd8) throw std::runtime_error("Invalid SOI");
        iter += 2;

        // Make sure we at least have enough to read the marker
        while ((iter + 4) < header.end()) {
            if (iter[0] != 0xff) throw std::runtime_error("Expected 0xff in marker");
            uint8_t marker = iter[1];
            iter += 2;

            // len includes the bytes themselves, but we don't want to consume them
            uint16_t len = (iter[0] << 8) | iter[1];
            LOGV(TAG, "Encountered marker %02x of length %d", marker, len);
            if (marker != 0xe2) {
                iter += len;
                continue;
            }

            LOGD(TAG, "Found APP2 marker, extracting metadata");
            // consume the length too, we want to point to the data
            const char *payload = header.data() + (iter - header.begin()) + 2;
            size_t payloadLen = len - 2;

            if (payloadLen < sizeof(Timestamp)) throw std::runtime_error("APP2 marker not big enough for timestamp");

            size_t strLen = payloadLen - sizeof(Timestamp);
            Timestamp timestamp;
            std::memcpy(&timestamp, payload + strLen, sizeof(Timestamp));

            std::string title;
            bool isAscii = std::all_of(payload, payload + strLen, [](char b) { return b <= 0x7F; });
            if (isAscii) {
                title = trimTrailingSpaces(std::string(payload, strLen));
            }
            return std::pair(title, timestamp);
        }

        LOGE(TAG, "Couldn't find APP2 marker in JPEG, no metadata");

    } catch (std::exception &e) {
        LOGE(TAG, "%s", e.what());
    }
    return {};
}

void postProcess(std::string fileName) {
    std::string dir = "/";
    auto meta = getMetaFromJpegMarker(dir + fileName);

    time_t time = timestampToTime(meta.second);
    setSystemTime(time);

    LOGI(TAG, "File %s has metadata time=%s title='%s'", fileName.c_str(), std::ctime(&time), meta.first.c_str());

    // TODO make a new filename using the timestamp
    std::string base = fileName;

    if (meta.first.size() > 0) {
        auto file = FFat.open((dir + base + ".txt").c_str(), "w", true);
        file.println(meta.first.c_str());
        file.close();
    }

    // rename to new filename -- to apply a new creation date, we'll probably need to make a copy, ugh
    // auto file = FFat.open((dir + fileName).c_str(), "a"); // Touch won't do it
    // file.close();
    FFat.rename((dir + fileName).c_str(), ("/" + base + ".jpg").c_str());
}

}  // namespace Meta