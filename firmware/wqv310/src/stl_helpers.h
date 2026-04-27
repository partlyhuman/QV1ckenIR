#pragma once

#include <array>
#include <span>
#include <string>

#include "log.h"

static_assert(__cplusplus >= 202002L, "C++20 required for std::span");

template <typename... Bytes>
static bool expectData(const uint8_t *readBuffer, Bytes... bytes) {
    constexpr size_t N = sizeof...(Bytes);
    auto arr = std::span(readBuffer);
    if (arr.size() < N) {
        LOGE("STL", "Data is shorter than expected pattern %d bytes", N);
        return false;
    }
    size_t i = 0;
    bool ret = ((arr[i++] == static_cast<uint8_t>(bytes)) && ...);
    if (!ret) {
        LOGE("STL", "Data does not start with expected pattern");
    }
    return ret;
}

template <typename First, typename... Rest>
auto concat(const First &first, const Rest &...rest) {
    using T = typename First::value_type;
    std::vector<T> result;

    size_t totalSize = first.size() + (rest.size() + ...);
    result.reserve(totalSize);

    result.insert(result.end(), first.begin(), first.end());
    (result.insert(result.end(), rest.begin(), rest.end()), ...);

    return result;
}

// template <typename... Spans>
// std::vector<uint8_t> concat(Spans... spans) {
//     std::vector<uint8_t> result;
//     size_t totalSize = (spans.size() + ... + 0);
//     result.reserve(totalSize);
//     (result.insert(result.end(), spans.begin(), spans.end()), ...);
//     return result;
// }

void appendSpan(std::vector<uint8_t> vec, std::span<const uint8_t> data) {
    vec.insert(vec.end(), data.begin(), data.end());
}

template <typename T>
void appendStruct(std::vector<uint8_t> vec, T obj) {
    auto *begin = reinterpret_cast<const uint8_t *>(&obj);
    auto *end = begin + sizeof(T);
    vec.insert(vec.end(), begin, end);
}

template <size_t N>
bool startsWith(std::span<const uint8_t> buf, const uint8_t (&lit)[N]) {
    return buf.size() >= N && std::equal(buf.begin(), buf.begin() + N, lit);
}