#include "jpr/keycodes.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jpr {

namespace {

struct Entry {
    const char* name;
    int code;
};

const Entry kKeys[] = {
    {"backspace", 8},   {"tab", 9},         {"enter", 13},      {"return", 13},
    {"shift", 16},      {"ctrl", 17},       {"control", 17},    {"pause", 19},
    {"capslock", 20},   {"escape", 27},     {"esc", 27},        {"space", 32},
    {"pageup", 33},     {"pagedown", 34},   {"end", 35},        {"home", 36},
    {"left", 37},       {"up", 38},         {"right", 39},      {"down", 40},
    {"insert", 45},     {"delete", 46},     {"alt", 0x12},      {"menu", 255},
    {"numlock", 144},   {"scrolllock", 145},{"semicolon", 186}, {"equal", 187},
    {"comma", 188},     {"minus", 189},     {"period", 190},    {"slash", 191},
    {"grave", 192},     {"leftbracket", 219},{"backslash", 220},{"rightbracket", 221},
    {"apostrophe", 222},
};

// Strips separators and lowercases so "page_up", "Page Up" and "pageup" match.
void normalise(const char* in, char* out, size_t outSize) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < outSize; i++) {
        char c = in[i];
        if (c == '_' || c == '-' || c == ' ')
            continue;
        out[j++] = (char)tolower((unsigned char)c);
    }
    out[j] = '\0';
}

}  // namespace

int parseKeyName(const char* name, int fallback) {
    if (!name || !*name)
        return fallback;

    char buf[32];
    normalise(name, buf, sizeof(buf));
    if (!buf[0])
        return fallback;

    // Numeric forms: "32" and "0x20".
    char* end = nullptr;
    long numeric = strtol(buf, &end, 0);
    if (end && *end == '\0' && numeric > 0 && numeric < 512)
        return (int)numeric;

    for (auto const& entry : kKeys) {
        if (!strcmp(entry.name, buf))
            return entry.code;
    }

    // Single letters and digits map to their ASCII uppercase value.
    if (buf[1] == '\0') {
        if (buf[0] >= 'a' && buf[0] <= 'z')
            return buf[0] - 'a' + 65;
        if (buf[0] >= '0' && buf[0] <= '9')
            return buf[0] - '0' + 48;
    }

    // Function keys f1..f12.
    if (buf[0] == 'f' && buf[1]) {
        long n = strtol(buf + 1, &end, 10);
        if (end && *end == '\0' && n >= 1 && n <= 12)
            return 111 + (int)n;
    }

    return fallback;
}

const char* keyName(int code) {
    code &= 0xff;
    for (auto const& entry : kKeys) {
        if (entry.code == code)
            return entry.name;
    }
    static char buf[8];
    if (code >= 65 && code <= 90) {
        buf[0] = (char)('a' + code - 65);
        buf[1] = '\0';
        return buf;
    }
    if (code >= 48 && code <= 57) {
        buf[0] = (char)('0' + code - 48);
        buf[1] = '\0';
        return buf;
    }
    if (code >= 112 && code <= 123) {
        snprintf(buf, sizeof(buf), "f%d", code - 111);
        return buf;
    }
    return "unknown";
}

}  // namespace jpr
