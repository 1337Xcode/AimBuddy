#ifndef ESP_LOGGER_H
#define ESP_LOGGER_H

#include <android/log.h>
#include "../settings.h"

/**
 * @file Logger.h
 * @brief Android logcat wrapper macros for consistent logging
 * 
 * Provides LOGD, LOGI, LOGW, LOGE macros that wrap __android_log_print.
 * Debug logs can be disabled in production via ENABLE_DEBUG_LOGGING.
 */

// Debug log (can be disabled in production)
#if ENABLE_DEBUG_LOGGING
    #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, Config::LOG_TAG, __VA_ARGS__)
#else
    #define LOGD(...) ((void)0)
#endif

// Info log
#if ENABLE_INFO_LOGGING
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, Config::LOG_TAG, __VA_ARGS__)
#else
    #define LOGI(...) ((void)0)
#endif

// Warning log
#if ENABLE_WARN_LOGGING
    #define LOGW(...) __android_log_print(ANDROID_LOG_WARN, Config::LOG_TAG, __VA_ARGS__)
#else
    #define LOGW(...) ((void)0)
#endif

// Error log
#if ENABLE_ERROR_LOGGING
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, Config::LOG_TAG, __VA_ARGS__)
#else
    #define LOGE(...) ((void)0)
#endif

// Profiling log (can be disabled)
#if ENABLE_PROFILING
    #define LOGP(...) __android_log_print(ANDROID_LOG_INFO, Config::LOG_TAG, __VA_ARGS__)
#else
    #define LOGP(...) ((void)0)
#endif

#endif // ESP_LOGGER_H
