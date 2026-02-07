#include "image.h"

#include <string>
#include <unordered_map>

#include "FFat.h"
#include "ctime"
#include "display.h"
#include "log.h"

// Must define this before including stb_image_write
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../lib/stb_image_write.h"

// Which metadata format/s should we use? Feel free to enable multiple or none.
#define META_TITLE_IFNOTBLANK_FORMAT
#undef META_INI_FORMAT
#undef META_JSON_FORMAT
// Which image format/s should we use? Feel free to enable multiple.
#define IMAGE_PNG_FORMAT
#undef IMAGE_JPG_FORMAT
#undef IMAGE_BMP_FORMAT

namespace Image {

static const char *TAG = "Image";

static uint8_t expanded[W * H];

bool init() {
    return true;
}

time_t setSystemTime(const Image *img) {
    tm t{};
    t.tm_year = (2000 + img->year_minus_2000) - 1900;
    t.tm_mon = img->month - 1;
    t.tm_mday = img->day;
    t.tm_hour = img->hour;
    t.tm_min = img->minute;
    t.tm_isdst = 0;
    time_t time = mktime(&t);

    timeval epoch = {time, 0};
    settimeofday((const timeval *)&epoch, 0);
    return time;
}

static inline std::string pad2(int v) {
    return v < 10 ? "0" + std::to_string(v) : std::to_string(v);
}

void stbi_write_cb(void *context, void *data, int size) {
    ((File *)context)->write((uint8_t *)data, size);
}

bool save(const Image *img) {
    File f;

    // Convert Casio pixel data to 8bpp 1-channel grayscale image
    for (int i = 0; i < W * H; i++) {
        // two pixels stored per byte, in 2 nybbles
        uint8_t b = img->pixel[i / 2];
        uint8_t pixel = (i % 2 == 0) ? b & 0xf : b >> 4;
        // 0 is white (reverse of normal), expand 4 bits to 0-255 (15 * 17 = 255)
        expanded[i] = 255 - pixel * 17;
    }

    // Filenames need to start with / and the base path of /ffat is transparently handled by FFat
    std::string basepath, filename;

    // try to set timestamp when writing file
    time_t time = setSystemTime(img);

    // File naming strategies:
    // - count: simple, short filenames, will always collide with multiple syncs. could persist last count
    // static int count = 0;
    // basepath = "/WQV" + pad2(++count);
    // - including whole date and time, plus count: legible, no collisions, but long
    // basepath = "/WQV_" + std::to_string(img->month) + "-" + std::to_string(img->day) + "-" +
    //            std::to_string(2000 + img->year_minus_2000) + "_" + pad2(img->hour) + "-" + pad2(img->minute);
    // - unix style timestamp, plus count: shorter, no collisions, not very legible
    basepath = "/WQV_" + std::to_string(time);

    // try to only set count with overlapping timestamps (note this is never freed)
    static std::unordered_map<time_t, size_t> timestampCounts;
    int count = timestampCounts[time]++;
    if (count > 0) basepath += "_" + std::to_string(count);

#ifdef IMAGE_PNG_FORMAT
    filename = basepath + ".png";
    LOGI(TAG, "Writing image to %s...", filename.c_str());
    if ((f = FFat.open(filename.c_str(), FILE_WRITE, true))) {
        stbi_write_png_to_func(stbi_write_cb, &f, W, H, 1, expanded, W);
        f.close();
    }
#endif
#ifdef IMAGE_JPG_FORMAT
    filename = basepath + ".jpg";
    LOGI(TAG, "Writing image to %s...", filename.c_str());
    if ((f = FFat.open(filename.c_str(), FILE_WRITE, true))) {
        stbi_write_jpg_to_func(stbi_write_cb, &f, W, H, 1, expanded, 90);
        f.close();
    }
#endif
#ifdef IMAGE_BMP_FORMAT
    filename = basepath + ".bmp";
    LOGI(TAG, "Writing image to %s...", filename.c_str());
    if ((f = FFat.open(filename.c_str(), FILE_WRITE, true))) {
        stbi_write_bmp_to_func(stbi_write_cb, &f, W, H, 1, expanded);
        f.close();
    }
#endif

// Write meta info
#ifdef META_TITLE_IFNOTBLANK_FORMAT
    std::string title(img->name, sizeof(Image::name));
    if (title.find_first_not_of(' ') != std::string::npos) {
        f = FFat.open((basepath + ".txt").c_str(), FILE_WRITE, true);
        if (f) {
            title = title.substr(0, title.find_last_not_of(' ') + 1);
            f.print(title.c_str());
            f.close();
        }
    }
#endif
#ifdef META_INI_FORMAT
    f = FFat.open((basepath + ".txt").c_str(), FILE_WRITE, true);
    if (f) {
        f.printf("date = %04d-%02d-%02dT%02d:%02d\n", 2000 + img->year_minus_2000, img->month, img->day, img->hour,
                 img->minute);
        f.print("title = ");
        f.write((uint8_t *)img->name, sizeof(Image::name));
        f.print("\n");
        f.flush();
        f.close();
    }
#endif
#ifdef META_JSON_FORMAT
    f = FFat.open((basepath + ".json").c_str(), FILE_WRITE, true);
    if (f) {
        f.printf("{\n\t\"date\": \"%04d-%02d-%02dT%02d:%02d\",", 2000 + img->year_minus_2000, img->month, img->day,
                 img->hour, img->minute);
        f.print("\n\t\"title\": \"");
        f.write((uint8_t *)img->name, sizeof(Image::name));
        f.print("\"\n}\n");
        f.flush();
        f.close();
    }
#endif

    LOGI(TAG, "After writing: %d/%d total %d", FFat.usedBytes(), FFat.freeBytes(), FFat.totalBytes());

    return true;
}

void exportImagesFromDump(File &dump) {
    if (!dump) {
        LOGE(TAG, "No file!");
        return;
    }
    size_t count = dump.size() / sizeof(Image);
    dump.seek(0);
    // We'll reuse a single Image, but we want it on the heap, it's big
    Image *img = new Image{};
    for (size_t i = 0; i < count; i++) {
        Display::showProgressScreen(i, count, 1, "CONVERTING");
        LOGD(TAG, "Reading out image %d/%d", i, count);
        dump.readBytes(reinterpret_cast<char *>(img), sizeof(Image));
        save(img);
    }
    delete img;

    // Forced 100% screen
    Display::showProgressScreen(9999, 10000, 10000 / count, "CONVERTING");
}

}  // namespace Image