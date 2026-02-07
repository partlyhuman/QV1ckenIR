#pragma once

// 0 - Errors, 1 - Warnings, 2 - Info, 3 - Debug, 4 - Verbose
#ifndef LOG_LEVEL
#define LOG_LEVEL 2
#endif

#define ANSI_RED "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RESET "\033[0m"

#if LOG_LEVEL >= 0
#define LOGE(tag, format, ...) \
  Serial.printf(ANSI_RED "E[%s] " format ANSI_RESET "\n", tag, ##__VA_ARGS__)
#else
#define LOGE(tag, format, ...)
#endif

#if LOG_LEVEL >= 1
#define LOGW(tag, format, ...) \
  Serial.printf(ANSI_YELLOW "W[%s] " format ANSI_RESET "\n", tag, ##__VA_ARGS__)
#else
#define LOGW(tag, format, ...)
#endif

#if LOG_LEVEL >= 2
#define LOGI(tag, format, ...) \
  Serial.printf("I[%s] " format "\n", tag, ##__VA_ARGS__)
#else
#define LOGI(tag, format, ...)
#endif

#if LOG_LEVEL >= 3
#define LOGD(tag, format, ...) \
  Serial.printf("D[%s] " format "\n", tag, ##__VA_ARGS__)
#else
#define LOGD(tag, format, ...)
#endif

#if LOG_LEVEL >= 4
#define LOGV(tag, format, ...) \
  Serial.printf("V[%s] " format "\n", tag, ##__VA_ARGS__)
#else
#define LOGV(tag, format, ...)
#endif
