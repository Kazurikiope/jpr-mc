#pragma once

namespace jpr {

// Minecraft/Windows virtual key codes, matching game-window's KeyMapping.
// The game masks these with 0xff, so the 256 bit that marks the right hand
// variant of a modifier is irrelevant for injection.
namespace keycode {
constexpr int Backspace = 8;
constexpr int Tab = 9;
constexpr int Enter = 13;
constexpr int Shift = 16;
constexpr int Ctrl = 17;
constexpr int Alt = 0x12;
constexpr int Escape = 27;
constexpr int Space = 32;
}  // namespace keycode

// Resolves "space", "SPACE", "w", "f5", "0x20" or "32" to a key code.
// Returns `fallback` for anything it does not recognise.
int parseKeyName(const char* name, int fallback);

// Canonical lowercase name for a key code, or "unknown".
const char* keyName(int code);

}  // namespace jpr
