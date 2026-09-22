// Minimal mirror of the GameActivity ABI.
//
// Newer Minecraft builds no longer export the Keyboard statics the launcher
// prefers to write, and fall back to Android's GameActivity: the launcher
// hands the game a GameActivity whose `callbacks` table the game fills in
// during GameActivity_onCreate, then delivers key presses by calling
// `callbacks->onKeyDown(activity, event)` (see JniSupport::startGame and
// WindowCallbacks::onKeyboard in mcpelauncher-client). This mod does the same
// thing, so it needs the layout of those structs.
//
// These are mirrored rather than included because the real headers need jni.h
// and the android/* sysroot, which the host test build does not have.
// src/game_activity_abi_check.cpp includes the real ones in the Android build
// and static_asserts every offset used here — a wrong offset would call the
// wrong function pointer instead of failing visibly, so it is checked in CI
// rather than trusted.
#pragma once

#include <cstddef>
#include <cstdint>

namespace jpr {
namespace ga {

// android/game_activity_events.h
struct KeyEvent {
    int32_t deviceId;
    int32_t source;
    int32_t action;
    int64_t eventTime;
    int64_t downTime;
    int32_t flags;
    int32_t metaState;
    int32_t modifiers;
    int32_t repeatCount;
    int32_t keyCode;
    int32_t scanCode;
    int32_t unicodeChar;
};

struct Activity;

// android/game_activity.h. Every member is a function pointer, in this order;
// only onKeyDown and onKeyUp are called, but the ones before them have to be
// present for the offsets to land.
struct Callbacks {
    void (*onStart)(Activity*);
    void (*onResume)(Activity*);
    void* (*onSaveInstanceState)(Activity*, void (*)(const char*, int, void*), void*);
    void (*onPause)(Activity*);
    void (*onStop)(Activity*);
    void (*onDestroy)(Activity*);
    void (*onWindowFocusChanged)(Activity*, bool);
    void (*onNativeWindowCreated)(Activity*, void*);
    void (*onNativeWindowResized)(Activity*, void*, int32_t, int32_t);
    void (*onNativeWindowRedrawNeeded)(Activity*, void*);
    void (*onNativeWindowDestroyed)(Activity*, void*);
    void (*onConfigurationChanged)(Activity*);
    void (*onTrimMemory)(Activity*, int);
    bool (*onTouchEvent)(Activity*, const void*);
    bool (*onKeyDown)(Activity*, const KeyEvent*);
    bool (*onKeyUp)(Activity*, const KeyEvent*);
    void (*onTextInputEvent)(Activity*, const void*);
};

// Only the leading `callbacks` pointer is needed; the rest of the struct is
// the launcher's business.
struct Activity {
    Callbacks* callbacks;
};

// GameActivity_onCreate. Hooked to capture the Activity the launcher passes in,
// since there is no other way to reach it from a mod.
using CreateFunc = void (*)(Activity* activity, void* savedState, size_t savedStateSize);

// Stable Android input constants, from android/input.h and android/keycodes.h.
constexpr int32_t kSourceKeyboard = 0x00000101;  // AINPUT_SOURCE_KEYBOARD
constexpr int32_t kActionDown = 0;               // AKEY_EVENT_ACTION_DOWN
constexpr int32_t kActionUp = 1;                 // AKEY_EVENT_ACTION_UP
constexpr int32_t kKeyCodeUnknown = 0;           // AKEYCODE_UNKNOWN

// Translates a Minecraft/Windows virtual key code to an Android key code,
// mirroring WindowCallbacks::mapMinecraftToAndroidKey so an injected press is
// indistinguishable from one the launcher forwards.
// Returns kKeyCodeUnknown for anything unmapped.
//
// constexpr so game_activity_abi_check.cpp can static_assert the results
// against the real AKEYCODE_* macros; the values below are written as literals
// because this header is also compiled for the host tests, where
// android/keycodes.h does not exist.
constexpr int32_t androidKeyCode(int minecraftKey) {
    // Ranges first, in the same order the launcher checks them.
    if (minecraftKey >= 48 && minecraftKey <= 57)      // NUM_0..NUM_9
        return minecraftKey - 48 + 7;                  // AKEYCODE_0
    if (minecraftKey >= 0x60 && minecraftKey <= 0x69)  // NUMPAD_0..NUMPAD_9
        return minecraftKey - 0x60 + 144;              // AKEYCODE_NUMPAD_0
    if (minecraftKey >= 65 && minecraftKey <= 90)      // A..Z
        return minecraftKey - 65 + 29;                 // AKEYCODE_A
    if (minecraftKey >= 112 && minecraftKey <= 123)    // FN1..FN12
        return minecraftKey - 112 + 131;               // AKEYCODE_F1

    switch (minecraftKey) {
    case 4: return 4;       // BACK
    case 8: return 67;      // BACKSPACE -> AKEYCODE_DEL
    case 9: return 61;      // TAB
    case 13: return 66;     // ENTER
    case 16: return 59;     // SHIFT -> AKEYCODE_SHIFT_LEFT
    case 17: return 113;    // CTRL -> AKEYCODE_CTRL_LEFT
    case 0x12: return 57;   // ALT -> AKEYCODE_ALT_LEFT
    case 19: return 121;    // PAUSE -> AKEYCODE_BREAK
    case 20: return 115;    // CAPS_LOCK
    case 27: return 111;    // ESCAPE
    case 32: return 62;     // SPACE
    case 33: return 92;     // PAGE_UP
    case 34: return 93;     // PAGE_DOWN
    case 35: return 123;    // END -> AKEYCODE_MOVE_END
    case 36: return 122;    // HOME -> AKEYCODE_MOVE_HOME
    case 37: return 21;     // LEFT -> AKEYCODE_DPAD_LEFT
    case 38: return 19;     // UP -> AKEYCODE_DPAD_UP
    case 39: return 22;     // RIGHT -> AKEYCODE_DPAD_RIGHT
    case 40: return 20;     // DOWN -> AKEYCODE_DPAD_DOWN
    case 45: return 124;    // INSERT
    case 46: return 112;    // DELETE -> AKEYCODE_FORWARD_DEL
    case 144: return 143;   // NUM_LOCK
    case 145: return 116;   // SCROLL_LOCK
    case 186: return 74;    // SEMICOLON
    case 187: return 70;    // EQUAL -> AKEYCODE_EQUALS
    case 188: return 55;    // COMMA
    case 189: return 69;    // MINUS
    case 190: return 56;    // PERIOD
    case 191: return 76;    // SLASH
    case 192: return 68;    // GRAVE
    case 219: return 71;    // LEFT_BRACKET
    case 220: return 73;    // BACKSLASH
    case 221: return 72;    // RIGHT_BRACKET
    case 222: return 75;    // APOSTROPHE
    case 255: return 82;    // MENU
    case 0x6a: return 155;  // NUMPAD_MULTIPLY
    case 0x6b: return 157;  // NUMPAD_ADD
    case 0x6d: return 156;  // NUMPAD_SUBTRACT
    case 0x6e: return 158;  // NUMPAD_DECIMAL -> AKEYCODE_NUMPAD_DOT
    case 0x6f: return 154;  // NUMPAD_DIVIDE
    default: return kKeyCodeUnknown;
    }
}

}  // namespace ga
}  // namespace jpr
