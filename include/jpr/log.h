#pragma once

namespace jpr {

enum class LogLevel { Trace = 0, Debug, Info, Warn, Error };

// Messages below this level are dropped. Driven by `log_level` in jpr.json.
void setLogLevel(LogLevel level);
LogLevel logLevel();

// Parses "trace"/"debug"/"info"/"warn"/"error"; unknown text yields `fallback`.
LogLevel parseLogLevel(const char* text, LogLevel fallback);

void logMessage(LogLevel level, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

}  // namespace jpr

#define JPR_TRACE(...) ::jpr::logMessage(::jpr::LogLevel::Trace, JPR_LOG_TAG, __VA_ARGS__)
#define JPR_DEBUG(...) ::jpr::logMessage(::jpr::LogLevel::Debug, JPR_LOG_TAG, __VA_ARGS__)
#define JPR_INFO(...) ::jpr::logMessage(::jpr::LogLevel::Info, JPR_LOG_TAG, __VA_ARGS__)
#define JPR_WARN(...) ::jpr::logMessage(::jpr::LogLevel::Warn, JPR_LOG_TAG, __VA_ARGS__)
#define JPR_ERROR(...) ::jpr::logMessage(::jpr::LogLevel::Error, JPR_LOG_TAG, __VA_ARGS__)

#ifndef JPR_LOG_TAG
#define JPR_LOG_TAG "jpr"
#endif
