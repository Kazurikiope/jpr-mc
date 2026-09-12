#define JPR_LOG_TAG "jpr.sig"

#include "jpr/sigscan.h"

#include <cctype>
#include <cstring>

#include "jpr/log.h"

namespace jpr {
namespace sig {

namespace {

int hexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

}  // namespace

bool parsePattern(const std::string& text, Pattern& out, std::string* error) {
    out.bytes.clear();
    out.wildcard.clear();

    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (isspace((unsigned char)c)) {
            i++;
            continue;
        }

        // "\x48\x8B" style input, and a bare 0x prefix, both show up in
        // signature dumps; accept them rather than making the user reformat.
        if (c == '\\' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            i += 2;
            continue;
        }
        if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            i += 2;
            continue;
        }

        if (c == '?') {
            out.bytes.push_back(0);
            out.wildcard.push_back(true);
            i++;
            if (i < text.size() && text[i] == '?')
                i++;
            continue;
        }

        int high = hexDigit(c);
        if (high < 0) {
            if (error)
                *error = std::string("unexpected character '") + c + "' in pattern";
            return false;
        }
        if (i + 1 >= text.size()) {
            if (error)
                *error = "pattern ends on a half byte";
            return false;
        }
        int low = hexDigit(text[i + 1]);
        if (low < 0) {
            if (error)
                *error = std::string("expected a hex digit after '") + c + "'";
            return false;
        }
        out.bytes.push_back((uint8_t)((high << 4) | low));
        out.wildcard.push_back(false);
        i += 2;
    }

    if (out.bytes.empty()) {
        if (error)
            *error = "pattern is empty";
        return false;
    }

    // A pattern that starts with a wildcard matches in more places than the
    // author expects and makes scanning slower; refuse it.
    if (out.wildcard.front()) {
        if (error)
            *error = "pattern must start with a concrete byte, not a wildcard";
        return false;
    }

    size_t concrete = 0;
    for (bool w : out.wildcard) {
        if (!w)
            concrete++;
    }
    if (concrete < 4) {
        if (error)
            *error = "pattern has fewer than 4 concrete bytes and would match almost anywhere";
        return false;
    }

    if (error)
        error->clear();
    return true;
}

namespace {

// Scans from `from`, returning the first match or null.
const uint8_t* findFrom(const Pattern& pattern, const uint8_t* from, const uint8_t* end) {
    if (pattern.empty() || (size_t)(end - from) < pattern.size())
        return nullptr;

    const uint8_t first = pattern.bytes[0];
    const uint8_t* limit = end - pattern.size() + 1;

    while (from < limit) {
        // memchr on the (always concrete) first byte does the bulk of the work
        // far faster than a byte-at-a-time loop over a 100MB text section.
        const uint8_t* candidate = (const uint8_t*)memchr(from, first, (size_t)(limit - from));
        if (!candidate)
            return nullptr;

        size_t i = 1;
        for (; i < pattern.size(); i++) {
            if (!pattern.wildcard[i] && candidate[i] != pattern.bytes[i])
                break;
        }
        if (i == pattern.size())
            return candidate;

        from = candidate + 1;
    }
    return nullptr;
}

}  // namespace

void* scan(const Pattern& pattern, uintptr_t begin, uintptr_t end, size_t occurrence) {
    const uint8_t* cursor = (const uint8_t*)begin;
    const uint8_t* last = (const uint8_t*)end;

    for (size_t seen = 0;; seen++) {
        const uint8_t* match = findFrom(pattern, cursor, last);
        if (!match)
            return nullptr;
        if (seen == occurrence)
            return (void*)match;
        cursor = match + 1;
    }
}

size_t countMatches(const Pattern& pattern, uintptr_t begin, uintptr_t end, size_t limit) {
    const uint8_t* cursor = (const uint8_t*)begin;
    const uint8_t* last = (const uint8_t*)end;
    size_t count = 0;
    while (count < limit) {
        const uint8_t* match = findFrom(pattern, cursor, last);
        if (!match)
            break;
        count++;
        cursor = match + 1;
    }
    return count;
}

}  // namespace sig
}  // namespace jpr
