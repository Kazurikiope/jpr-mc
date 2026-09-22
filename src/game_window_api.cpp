// The launcher's game_window_* mod API.
//
// This is the piece that makes the GameActivity path safe. Delivering a key
// press means calling into the game's input system, and doing that from the
// mod's own timer thread races whatever the game thread is doing. The launcher
// publishes a swap-buffers callback that runs once per frame on the game
// thread, so the mod can pump from there instead — the same thread the
// launcher itself delivers input on, at the same rate the game samples it.
//
// The keyboard callback is the other half: on versions without
// Keyboard::_states there is no way to read key state back, so
// debug_trigger_key had nothing to watch. This gives it real presses.

#define JPR_LOG_TAG "jpr.gamewindow"

#include "game_window_api.h"

#include <dlfcn.h>

#include <atomic>

#include "jpr/keycodes.h"
#include "jpr/log.h"
#include "jpr/tick.h"

namespace jpr {
namespace gamewindow {

namespace {

// Signatures from CorePatches::loadGameWindowLibrary.
using GetPrimaryWindowFn = void* (*)();
using AddKeyboardCallbackFn = void (*)(void* handle, void* user, bool (*callback)(void*, int, int));
using AddSwapBuffersCallbackFn = void (*)(void* user, void (*callback)(void*, void*, void*));
using AddWindowCreationCallbackFn = void (*)(void* user, void (*callback)(void*));

GetPrimaryWindowFn gGetPrimaryWindow = nullptr;
AddKeyboardCallbackFn gAddKeyboardCallback = nullptr;
AddSwapBuffersCallbackFn gAddSwapBuffersCallback = nullptr;
AddWindowCreationCallbackFn gAddWindowCreationCallback = nullptr;

bool gAvailable = false;
std::atomic<bool> gFrameLive{false};
std::atomic<bool> gKeyStateAvailable{false};

// The launcher reports KeyCode values, which are the same Minecraft/Windows
// codes the rest of the mod uses, so no translation is needed.
std::atomic<bool> gKeyDown[512];

// Runs once per frame, on the game thread.
void onSwapBuffers(void*, void*, void*) {
    if (!gFrameLive.exchange(true)) {
        JPR_INFO("per-frame callback is live; ticking from the game thread");
        tick::useGameThreadSource(true);
    }
    tick::pump(true);
}

// action: 0 = release, 1 = press, 2 = repeat (KeyAction). Returning false lets
// the key carry on to the game, which is what we want — this only observes.
bool onKeyboard(void*, int keyCode, int action) {
    if (keyCode >= 0 && keyCode < 512)
        gKeyDown[keyCode].store(action != 0, std::memory_order_relaxed);
    return false;
}

// The window handle's callbacks are only wired up once the window exists, so
// the keyboard callback has to wait for that rather than being registered at
// mod_init, where the handle is still blank.
void onWindowCreated(void*) {
    if (!gGetPrimaryWindow || !gAddKeyboardCallback)
        return;
    void* handle = gGetPrimaryWindow();
    if (!handle) {
        JPR_WARN("no primary game window; key state will be unavailable");
        return;
    }
    gAddKeyboardCallback(handle, nullptr, &onKeyboard);
    gKeyStateAvailable.store(true);
    JPR_INFO("registered keyboard callback; real key presses are now visible");
}

}  // namespace

bool init() {
    void* lib = dlopen("libmcpelauncher_gamewindow.so", 0);
    if (!lib) {
        JPR_WARN("libmcpelauncher_gamewindow.so unavailable (%s); falling back to the timer tick source",
                 dlerror() ? dlerror() : "no error");
        return false;
    }

    gGetPrimaryWindow = (GetPrimaryWindowFn)dlsym(lib, "game_window_get_primary_window");
    gAddKeyboardCallback = (AddKeyboardCallbackFn)dlsym(lib, "game_window_add_keyboard_callback");
    gAddSwapBuffersCallback = (AddSwapBuffersCallbackFn)dlsym(lib, "game_window_add_swap_buffers_callback");
    gAddWindowCreationCallback =
        (AddWindowCreationCallbackFn)dlsym(lib, "game_window_add_window_creation_callback");

    JPR_INFO("game_window API: primary_window=%s keyboard_callback=%s swap_buffers=%s window_creation=%s",
             gGetPrimaryWindow ? "ok" : "missing", gAddKeyboardCallback ? "ok" : "missing",
             gAddSwapBuffersCallback ? "ok" : "missing", gAddWindowCreationCallback ? "ok" : "missing");

    // Registering the per-frame callback needs no window, so it can go in now.
    if (gAddSwapBuffersCallback) {
        gAddSwapBuffersCallback(nullptr, &onSwapBuffers);
        JPR_INFO("registered per-frame callback; it will take over ticking when the game starts rendering");
    }

    if (gAddWindowCreationCallback)
        gAddWindowCreationCallback(nullptr, &onWindowCreated);

    gAvailable = gAddSwapBuffersCallback != nullptr;
    return gAvailable;
}

bool available() { return gAvailable; }

bool frameCallbackLive() { return gFrameLive.load(); }

bool keyStateAvailable() { return gKeyStateAvailable.load(); }

bool keyDown(int minecraftKeyCode) {
    if (minecraftKeyCode < 0 || minecraftKeyCode >= 512)
        return false;
    return gKeyDown[minecraftKeyCode].load(std::memory_order_relaxed);
}

}  // namespace gamewindow
}  // namespace jpr
