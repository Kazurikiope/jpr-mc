#include "jpr/log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "jpr/api.h"

namespace jpr {

namespace {
std::atomic<int> gLevel{(int)LogLevel::Info};
}  // namespace

void setLogLevel(LogLevel level) { gLevel.store((int)level); }

LogLevel logLevel() { return (LogLevel)gLevel.load(); }

LogLevel parseLogLevel(const char* text, LogLevel fallback) {
    if (!text)
        return fallback;
    if (!strcasecmp(text, "trace")) return LogLevel::Trace;
    if (!strcasecmp(text, "debug")) return LogLevel::Debug;
    if (!strcasecmp(text, "info")) return LogLevel::Info;
    if (!strcasecmp(text, "warn") || !strcasecmp(text, "warning")) return LogLevel::Warn;
    if (!strcasecmp(text, "error")) return LogLevel::Error;
    return fallback;
}

void logMessage(LogLevel level, const char* tag, const char* fmt, ...) {
    if ((int)level < gLevel.load())
        return;

    char body[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    static const char* kNames[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR"};
    const char* name = kNames[(int)level];

    // Prefer the launcher's logger so mod output lands in the launcher log
    // alongside everything else; fall back to stderr when running host tests.
    if (api::androidLog) {
        // android LogPriority: VERBOSE=2 DEBUG=3 INFO=4 WARN=5 ERROR=6
        api::androidLog(2 + (int)level, tag, "%s", body);
        return;
    }
    fprintf(stderr, "[%s/%s] %s\n", name, tag, body);
}

}  // namespace jpr
