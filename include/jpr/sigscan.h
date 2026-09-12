// Byte pattern scanning over libminecraftpe.so.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jpr {
namespace sig {

// A parsed IDA style pattern: "48 8B 05 ?? ?? ?? ?? 48 85 C0".
// `??` (or `?`) matches any byte.
struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> wildcard;

    bool empty() const { return bytes.empty(); }
    size_t size() const { return bytes.size(); }
};

// Returns false and fills `error` when the text is not a usable pattern.
bool parsePattern(const std::string& text, Pattern& out, std::string* error);

// Finds the `occurrence`-th (0 based) match in [begin, end). Returns null when
// there is no such match.
void* scan(const Pattern& pattern, uintptr_t begin, uintptr_t end, size_t occurrence = 0);

// Counts matches, capped at `limit` so an over-broad pattern cannot make this
// crawl the whole image repeatedly. Used to warn about ambiguous signatures.
size_t countMatches(const Pattern& pattern, uintptr_t begin, uintptr_t end, size_t limit = 8);

}  // namespace sig
}  // namespace jpr
