// Compile-time verification of include/jpr/game_activity_abi.h.
//
// The mod calls function pointers out of GameActivityCallbacks by offset. A
// wrong offset calls the wrong function, which is far worse than failing to
// build, so the mirrored layout is checked against the real headers wherever
// they can be included — the Android build, which is the one that ships.
#if defined(__ANDROID__)

#include <android/game_activity.h>
#include <android/keycodes.h>

#include <cstddef>

#include "jpr/game_activity_abi.h"

namespace {

// The two entry points actually called.
static_assert(offsetof(::GameActivityCallbacks, onKeyDown) == offsetof(jpr::ga::Callbacks, onKeyDown),
              "GameActivityCallbacks::onKeyDown moved; the mirror in game_activity_abi.h is wrong");
static_assert(offsetof(::GameActivityCallbacks, onKeyUp) == offsetof(jpr::ga::Callbacks, onKeyUp),
              "GameActivityCallbacks::onKeyUp moved; the mirror in game_activity_abi.h is wrong");

// A couple of neighbours, so a member inserted earlier is caught even if it
// happens not to shift the two above.
static_assert(offsetof(::GameActivityCallbacks, onTouchEvent) == offsetof(jpr::ga::Callbacks, onTouchEvent),
              "GameActivityCallbacks::onTouchEvent moved; the mirror is wrong");
static_assert(offsetof(::GameActivityCallbacks, onTextInputEvent) == offsetof(jpr::ga::Callbacks, onTextInputEvent),
              "GameActivityCallbacks::onTextInputEvent moved; the mirror is wrong");
static_assert(offsetof(::GameActivityCallbacks, onStart) == offsetof(jpr::ga::Callbacks, onStart),
              "GameActivityCallbacks::onStart moved; the mirror is wrong");

// The callbacks pointer has to be first in GameActivity.
static_assert(offsetof(::GameActivity, callbacks) == offsetof(jpr::ga::Activity, callbacks),
              "GameActivity::callbacks is not where the mirror expects it");
static_assert(offsetof(jpr::ga::Activity, callbacks) == 0, "GameActivity::callbacks must be first");

// The event struct is passed by pointer into the game, so every field matters.
static_assert(sizeof(::GameActivityKeyEvent) == sizeof(jpr::ga::KeyEvent),
              "GameActivityKeyEvent changed size; the mirror is wrong");
#define JPR_CHECK_KEY_FIELD(name)                                                            \
    static_assert(offsetof(::GameActivityKeyEvent, name) == offsetof(jpr::ga::KeyEvent, name), \
                  "GameActivityKeyEvent::" #name " moved; the mirror is wrong")
JPR_CHECK_KEY_FIELD(deviceId);
JPR_CHECK_KEY_FIELD(source);
JPR_CHECK_KEY_FIELD(action);
JPR_CHECK_KEY_FIELD(eventTime);
JPR_CHECK_KEY_FIELD(downTime);
JPR_CHECK_KEY_FIELD(flags);
JPR_CHECK_KEY_FIELD(metaState);
JPR_CHECK_KEY_FIELD(modifiers);
JPR_CHECK_KEY_FIELD(repeatCount);
JPR_CHECK_KEY_FIELD(keyCode);
JPR_CHECK_KEY_FIELD(scanCode);
JPR_CHECK_KEY_FIELD(unicodeChar);
#undef JPR_CHECK_KEY_FIELD

// And the constants copied out of the android headers.
static_assert(jpr::ga::kSourceKeyboard == AINPUT_SOURCE_KEYBOARD, "AINPUT_SOURCE_KEYBOARD mismatch");
static_assert(jpr::ga::kActionDown == AKEY_EVENT_ACTION_DOWN, "AKEY_EVENT_ACTION_DOWN mismatch");
static_assert(jpr::ga::kActionUp == AKEY_EVENT_ACTION_UP, "AKEY_EVENT_ACTION_UP mismatch");

// The key codes androidKeyCode() returns are written as literals so the host
// build can compile them; check them against the real header here. Jump is the
// one that matters, but a wrong literal anywhere would misroute a key.
static_assert(jpr::ga::androidKeyCode(32) == AKEYCODE_SPACE, "SPACE mapping wrong");
static_assert(jpr::ga::androidKeyCode('W') == AKEYCODE_W, "W mapping wrong");
static_assert(jpr::ga::androidKeyCode('A') == AKEYCODE_A, "A mapping wrong");
static_assert(jpr::ga::androidKeyCode('Z') == AKEYCODE_Z, "Z mapping wrong");
static_assert(jpr::ga::androidKeyCode(16) == AKEYCODE_SHIFT_LEFT, "SHIFT mapping wrong");
static_assert(jpr::ga::androidKeyCode(17) == AKEYCODE_CTRL_LEFT, "CTRL mapping wrong");
static_assert(jpr::ga::androidKeyCode(0x12) == AKEYCODE_ALT_LEFT, "ALT mapping wrong");
static_assert(jpr::ga::androidKeyCode(27) == AKEYCODE_ESCAPE, "ESCAPE mapping wrong");
static_assert(jpr::ga::androidKeyCode(13) == AKEYCODE_ENTER, "ENTER mapping wrong");
static_assert(jpr::ga::androidKeyCode(8) == AKEYCODE_DEL, "BACKSPACE mapping wrong");
static_assert(jpr::ga::androidKeyCode(48) == AKEYCODE_0, "0 mapping wrong");
static_assert(jpr::ga::androidKeyCode(57) == AKEYCODE_9, "9 mapping wrong");
static_assert(jpr::ga::androidKeyCode(112) == AKEYCODE_F1, "F1 mapping wrong");
static_assert(jpr::ga::androidKeyCode(116) == AKEYCODE_F5, "F5 mapping wrong");
static_assert(jpr::ga::androidKeyCode(123) == AKEYCODE_F12, "F12 mapping wrong");
static_assert(jpr::ga::androidKeyCode(37) == AKEYCODE_DPAD_LEFT, "LEFT mapping wrong");
static_assert(jpr::ga::androidKeyCode(0x60) == AKEYCODE_NUMPAD_0, "NUMPAD_0 mapping wrong");
static_assert(jpr::ga::androidKeyCode(0x6e) == AKEYCODE_NUMPAD_DOT, "NUMPAD_DECIMAL mapping wrong");
static_assert(jpr::ga::androidKeyCode(9999) == AKEYCODE_UNKNOWN, "unmapped key should be UNKNOWN");

#endif  // __ANDROID__
