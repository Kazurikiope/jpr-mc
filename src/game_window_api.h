#pragma once

namespace jpr {
namespace gamewindow {

// Binds the launcher's game_window_* API and registers:
//
//   * a swap-buffers callback, which runs once per frame on the game thread
//     and becomes the mod's tick source
//   * a keyboard callback, which reports real key presses, so
//     debug_trigger_key works on versions where Keyboard::_states is gone
//
// Both come from libmcpelauncher_gamewindow.so, a synthetic library the
// launcher publishes for mods (CorePatches::loadGameWindowLibrary), so neither
// needs a signature. Returns whether the API was found.
bool init();

bool available();

// True once the per-frame callback has actually fired, i.e. the mod is being
// ticked from the game thread rather than from the fallback timer.
bool frameCallbackLive();

// Real key state, as reported by the launcher's keyboard callback.
bool keyDown(int minecraftKeyCode);
bool keyStateAvailable();

}  // namespace gamewindow
}  // namespace jpr
