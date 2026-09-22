// GameActivity key injection.
//
// Newer Minecraft builds do not export the Keyboard statics, and the launcher
// falls back to Android's GameActivity for input: it hands the game a
// GameActivity whose `callbacks` table the game populates during
// GameActivity_onCreate, then delivers each press by calling
// `callbacks->onKeyDown(activity, event)`.
//
// A mod cannot see the launcher's GameActivity — it lives in JniSupport, host
// side. But GameActivity_onCreate is an exported symbol of libminecraftpe.so
// (the launcher dlsym's it), so hooking it captures the pointer as it goes
// past, and after that the mod can deliver key events by exactly the same call
// the launcher makes. No version specific signature is involved.

#define JPR_LOG_TAG "jpr.keyboard.ga"

#include "keyboard_gameactivity.h"

#include "jpr/api.h"
#include "jpr/inline_hook.h"
#include "jpr/keycodes.h"
#include "jpr/log.h"

namespace jpr {
namespace keyboard {
namespace gameactivity {

namespace {

ga::CreateFunc gOriginalOnCreate = nullptr;
ga::Activity* gActivity = nullptr;
bool gSymbolFound = false;
bool gHooked = false;

void onCreateHook(ga::Activity* activity, void* savedState, size_t savedStateSize) {
    // The game fills in the callbacks table during this call, so capture the
    // activity only after it returns.
    if (gOriginalOnCreate)
        gOriginalOnCreate(activity, savedState, savedStateSize);

    gActivity = activity;
    if (activity && activity->callbacks) {
        JPR_INFO("captured GameActivity %p (onKeyDown=%p onKeyUp=%p)", (void*)activity,
                 (void*)activity->callbacks->onKeyDown, (void*)activity->callbacks->onKeyUp);
        if (!activity->callbacks->onKeyDown || !activity->callbacks->onKeyUp)
            JPR_ERROR("the game left onKeyDown/onKeyUp null; key injection cannot work this way");
    } else {
        JPR_ERROR("GameActivity_onCreate returned without a callbacks table");
    }
}

}  // namespace

bool install() {
    void* onCreate = api::minecraftSymbol("GameActivity_onCreate");
    gSymbolFound = onCreate != nullptr;
    if (!onCreate) {
        JPR_INFO("GameActivity_onCreate is not exported; this is not a GameActivity build");
        return false;
    }

    // Worth stating plainly: finding this symbol also proves symbol resolution
    // against libminecraftpe.so works at all, so a missing Keyboard::_states
    // really is missing rather than a lookup failure on our side.
    JPR_INFO("GameActivity_onCreate at %p", onCreate);

    gHooked = hook::install(onCreate, (void*)&onCreateHook, (void**)&gOriginalOnCreate,
                            "GameActivity_onCreate");
    if (!gHooked) {
        JPR_ERROR("could not hook GameActivity_onCreate; key injection unavailable");
        return false;
    }
    JPR_INFO("waiting for the game to start so the GameActivity can be captured");
    return true;
}

bool live() { return gActivity && gActivity->callbacks && gActivity->callbacks->onKeyDown; }

bool symbolFound() { return gSymbolFound; }
bool hooked() { return gHooked; }

bool send(int minecraftKey, bool down, int metaState) {
    if (!live())
        return false;

    int32_t androidKey = ga::androidKeyCode(minecraftKey);
    if (androidKey == ga::kKeyCodeUnknown) {
        JPR_WARN("no Android key code for %s (%d); not sending", keyName(minecraftKey), minecraftKey);
        return false;
    }

    // Built exactly like WindowCallbacks::onKeyboard builds it, fields the
    // launcher leaves zeroed included, so the game cannot tell the difference.
    ga::KeyEvent event = {};
    event.deviceId = 0;
    event.source = ga::kSourceKeyboard;
    event.action = down ? ga::kActionDown : ga::kActionUp;
    event.metaState = metaState;
    event.keyCode = androidKey;

    auto* callbacks = gActivity->callbacks;
    if (down)
        callbacks->onKeyDown(gActivity, &event);
    else if (callbacks->onKeyUp)
        callbacks->onKeyUp(gActivity, &event);
    else
        return false;
    return true;
}

}  // namespace gameactivity
}  // namespace keyboard
}  // namespace jpr
