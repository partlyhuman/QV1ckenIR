#include "image.h"

#include <FFat.h>

#include <ctime>
#include <format>
#include <span>
#include <unordered_map>

#include "log.h"
#include "wqv_jpeg_core.h"
#include "wqv_types.h"

namespace Image {

const static char *TAG = "Image";
static std::unordered_map<std::string, size_t> timestampCounts;

void init() {
    timestampCounts.clear();
}

void setSystemTime(const time_t time) {
    timeval epoch = {time, 0};
    settimeofday((const timeval *)&epoch, 0);
}

bool saveToFlash(std::span<const uint8_t> data, std::string destPath) {
    auto startTime = millis();
    File dst = FFat.open(destPath.c_str(), "w");
    if (!dst) {
        LOGE(TAG, "Couldn't open file for writing: %s", destPath.c_str());
        return false;
    }
    dst.write(data.data(), data.size());
    dst.close();
    LOGD(TAG, "Copied file in %d ms", millis() - startTime);
    return true;
}

std::string getBaseFilename(const Timestamp t) {
    std::string basepath;
    // File naming strategies:
    // - count: simple, short filenames, will always collide with multiple syncs. could persist last count
    // static int count = 0;
    // basepath = pad2(++count);
    // - including whole date and time, plus count: legible, no collisions, but long
    // basepath = std::to_string(img->month) + "-" + std::to_string(img->day) + "-" +
    //            std::to_string(2000 + img->year_minus_2000) + "_" + pad2(img->hour) + "-" + pad2(img->minute);
    // - just YYYYMMDD, plus count
    basepath = std::format("{:04}{:02}{:02}", 2000 + t.year2k, t.month, t.day);
    // - unix style timestamp, plus count: shorter, no collisions, not very legible
    // basepath = std::to_string(time);

    int count = timestampCounts[basepath]++;
    return std::format("WQV{}_{:02}", basepath, count + 1);
}

void postProcess(std::string fileName, std::vector<uint8_t> data, int wqvModel) {
    std::string dir = "/";

    // Parse proprietary metadata tag and replace with standard EXIF
    auto [title, timestamp] = parseCasioJpegMetadata(data, true, wqvModel);

    std::string base = getBaseFilename(timestamp);
    time_t time = timestampToTime(timestamp);
    setSystemTime(time);

    LOGI(TAG, "File %s has metadata time=%s title='%s'", fileName.c_str(), std::ctime(&time), title.c_str());

    // Write out the title into a sidecar file if it exists
    if (title.size() > 0) {
        auto file = FFat.open((dir + base + "_title.txt").c_str(), "w", true);
        if (file) {
            file.println(title.c_str());
            file.close();
        }
    }

    saveToFlash(data, dir + base + ".jpg");
}

}  // namespace Image