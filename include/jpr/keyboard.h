// Keyboard injection into libminecraftpe.so.
//
// The launcher feeds keyboard input to the game by writing two exported
// objects (see mcpelauncher-client/src/symbols.cpp):
//
//   Keyboard::_states  — int[256], the currently held state per key
//   Keyboard::_inputs  — a queue of press/release transitions
//
// Both are plain exported symbols, so injecting a key press needs nothing but
// dlsym — no signature scanning and no version specific offsets. That is why
// the jump half of this mod works on any version out of the box.
#pragma once

namespace jpr {
namespace keyboard {

enum class InjectMode {
    // Write the held-state array and queue the transition event. Matches what
    // the launcher itself does for a real key press.
    EventsAndStates,
    // Only write the held-state array. Nothing is pushed into the game's
    // queue, which removes the cross-thread container write — use this if the
    // fallback timer tick source ever looks unstable.
    StatesOnly,
    // Only queue transition events.
    EventsOnly,
};

InjectMode parseInjectMode(const char* text, InjectMode fallback);
const char* injectModeName(InjectMode mode);

void setInjectMode(InjectMode mode);

// Resolves the Keyboard symbols. Safe to call more than once; returns whether
// injection is usable.
bool init();
bool ready();

// Queues a press/release. Nothing reaches the game until flush() runs, so
// these are safe to call from any thread.
void press(int keyCode);
void release(int keyCode);

// True when this mod currently holds the key down.
bool holding(int keyCode);

// True when the game currently sees the key as held, whoever put it there.
// Reads Keyboard::_states, so it reflects the player's own keyboard as well as
// anything this mod injected.
bool gameKeyDown(int keyCode);

// Hands queued transitions to the game. Must run on the game thread when the
// inject mode queues events.
void flush();

// Test support: makes ready() true and records transitions into a log rather
// than touching the game. `capture` receives (keyCode, down) on every flush.
void enableTestCapture(void (*capture)(int keyCode, bool down));

// Releases everything this mod is holding. Used when a module is switched off
// mid-press so a key is never left stuck down.
void releaseAll();

}  // namespace keyboard
}  // namespace jpr
