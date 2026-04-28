#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "log.h"

// TODO clean up this crap

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

int findIndexOf(std::span<const uint8_t> buf, std::span<const uint8_t> pattern) {
    auto iter = std::search(buf.begin(), buf.end(), pattern.begin(), pattern.end());
    if (iter == buf.end()) return -1;
    return std::distance(buf.begin(), iter);
}