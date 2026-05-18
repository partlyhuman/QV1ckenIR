#include <unity.h>

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "wqv_jpeg_core.h"

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

std::vector<uint8_t> load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    TEST_ASSERT_TRUE(file.is_open());

    file.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    file.close();

    return data;
}

void save(std::span<const uint8_t> data, const std::string& path) {
    std::ofstream file(path, std::ios::binary);

    TEST_ASSERT_TRUE(file.is_open());

    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
}

void test_load_file() {
    const std::string path = std::filesystem::path(__FILE__).parent_path() / "jpeg_with_title.jpg";
    auto data = load(path);
    TEST_ASSERT_EQUAL(4204, data.size());
}

void test_parse_title() {
    const std::string path = std::filesystem::path(__FILE__).parent_path() / "jpeg_with_title.jpg";
    auto data = load(path);
    auto [title, timestamp] = parseCasioJpegMetadata(data);

    TEST_ASSERT_EQUAL_STRING("A", title.c_str());
}

void test_parse_time() {
    const std::string path = std::filesystem::path(__FILE__).parent_path() / "known_time.jpg";
    auto data = load(path);
    auto [title, timestamp] = parseCasioJpegMetadata(data);

    // I shot this 5/16 14:36 2026
    TEST_ASSERT_EQUAL(26, timestamp.year2k);
    TEST_ASSERT_EQUAL(5, timestamp.month);
    TEST_ASSERT_EQUAL(16, timestamp.day);
    TEST_ASSERT_EQUAL(14, timestamp.hour);
    TEST_ASSERT_EQUAL(36, timestamp.minute);
}

void test_erase_casio_jpeg_tags() {
    const std::string path = std::filesystem::path(__FILE__).parent_path() / "jpeg_with_title.jpg";
    auto data = load(path);
    size_t originalSize = data.size();
    parseCasioJpegMetadata(data, true);

    TEST_ASSERT_LESS_THAN(originalSize, data.size());

    // Must manually test that this is a valid JPEG
    save(data, std::filesystem::path(__FILE__).parent_path() / "OUT_jpeg_with_title_stripped.jpg");
}

void test_make_exif() {
    const std::string path = std::filesystem::path(__FILE__).parent_path() / "jpeg_with_title.jpg";
    auto data = load(path);
    auto [title, timestamp] = parseCasioJpegMetadata(data, true);

    auto exif = makeExifBlob(timestamp, title, 10);
    TEST_ASSERT_GREATER_THAN(0, exif.size());
    // Insert the exif jpeg tag first (after SOI mark)
    data.insert(data.begin() + 2, exif.begin(), exif.end());
    TEST_MESSAGE("Did we crash? no?\n");

    // Must manually test that this is a valid JPEG
    save(exif, std::filesystem::path(__FILE__).parent_path() / "OUT_jpeg_with_title_exif.jpg");
}

// Exposes a bug in the MicroExif impl
void test_make_exif_short_titles() {
    const std::string path = std::filesystem::path(__FILE__).parent_path() / "jpeg_with_title.jpg";
    auto original = load(path);
    Timestamp timestamp{.year2k = 0, .month = 1, .day = 1, .hour = 12, .minute = 30};

    std::vector<uint8_t> data, exif;

    data = std::vector(original);
    exif = makeExifBlob(timestamp, "1", 3);
    data.insert(data.begin() + 2, exif.begin(), exif.end());
    save(data, std::filesystem::path(__FILE__).parent_path() / "OUT_jpeg_with_title_1.jpg");

    data = std::vector(original);
    exif = makeExifBlob(timestamp, "12", 3);
    data.insert(data.begin() + 2, exif.begin(), exif.end());
    save(data, std::filesystem::path(__FILE__).parent_path() / "OUT_jpeg_with_title_2.jpg");

    data = std::vector(original);
    exif = makeExifBlob(timestamp, "123", 3);
    data.insert(data.begin() + 2, exif.begin(), exif.end());
    save(data, std::filesystem::path(__FILE__).parent_path() / "OUT_jpeg_with_title_3.jpg");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_load_file);
    RUN_TEST(test_parse_title);
    RUN_TEST(test_parse_time);
    RUN_TEST(test_erase_casio_jpeg_tags);
    RUN_TEST(test_make_exif);
    // RUN_TEST(test_make_exif_short_titles);
    return UNITY_END();
}
