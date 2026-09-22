#define JPR_LOG_TAG "jpr.keyboard"

#include "jpr/keyboard.h"

#include <cstring>
#include <mutex>
#include <vector>

#include "jpr/api.h"
#include "jpr/keycodes.h"
#include "jpr/log.h"
#include "jpr/status.h"
#include "keyboard_gameactivity.h"
#include "game_window_api.h"

namespace jpr {
namespace keyboard {

namespace {

// Mirrors mcpelauncher-client/src/symbols.h. The padding on `key` is
// deliberate: the game's generated code reads the full word.
struct InputEvent {
    int event;
    unsigned int key;
    int controllerId;
    int modShift, modCtrl, modAlt;
};

struct LegacyInputEvent {
    int event;
    unsigned int key;
    int controllerId;
};

struct Pending {
    int key;
    bool down;
};

std::mutex gMutex;
std::vector<Pending> gPending;
bool gHeld[256] = {};

bool gInitialised = false;
bool gLegacy = false;
Backend gBackend = Backend::None;
InjectMode gMode = InjectMode::EventsAndStates;

int* gStates = nullptr;
int* gControllerId = nullptr;
std::vector<InputEvent>* gInputs = nullptr;
std::vector<LegacyInputEvent>* gLegacyInputs = nullptr;

void (*gCapture)(int, bool) = nullptr;

void queue(int keyCode, bool down) {
    if (keyCode <= 0 || keyCode > 0xff) {
        JPR_WARN("refusing to inject out of range key code %d", keyCode);
        return;
    }
    std::lock_guard<std::mutex> lock(gMutex);
    if (gHeld[keyCode] == down)
        return;  // already in that state; nothing to send
    gHeld[keyCode] = down;
    gPending.push_back(Pending{keyCode, down});
    if (down)
        status::countPressQueued();
}

}  // namespace

InjectMode parseInjectMode(const char* text, InjectMode fallback) {
    if (!text)
        return fallback;
    if (!strcmp(text, "events_and_states"))
        return InjectMode::EventsAndStates;
    if (!strcmp(text, "states_only"))
        return InjectMode::StatesOnly;
    if (!strcmp(text, "events_only"))
        return InjectMode::EventsOnly;
    return fallback;
}

const char* injectModeName(InjectMode mode) {
    switch (mode) {
    case InjectMode::StatesOnly: return "states_only";
    case InjectMode::EventsOnly: return "events_only";
    default: return "events_and_states";
    }
}

void setInjectMode(InjectMode mode) {
    if (gMode == mode)
        return;
    releaseAll();
    gMode = mode;
    JPR_INFO("input inject mode set to %s", injectModeName(mode));
}

bool init() {
    if (gInitialised)
        return ready();
    gInitialised = true;

    if (!api::minecraftHandle()) {
        JPR_ERROR("libminecraftpe.so is not loaded yet");
        return false;
    }

    // The launcher uses the presence of bgfx_init to tell the modern keyboard
    // layout from the pre-bgfx one; match that exactly rather than guessing
    // from the version number.
    gLegacy = api::minecraftSymbol("bgfx_init") == nullptr;

    gStates = (int*)api::minecraftSymbol("_ZN8Keyboard7_statesE");
    gControllerId = (int*)api::minecraftSymbol("_ZN8Keyboard17_gameControllerIdE");
    void* inputs = api::minecraftSymbol("_ZN8Keyboard7_inputsE");
    if (gLegacy)
        gLegacyInputs = (std::vector<LegacyInputEvent>*)inputs;
    else
        gInputs = (std::vector<InputEvent>*)inputs;

    // Report each symbol separately. Which one is missing decides what can be
    // done about it, so a combined message is no use.
    JPR_INFO("Keyboard::_states           %s", gStates ? "found" : "MISSING");
    JPR_INFO("Keyboard::_inputs           %s", inputs ? "found" : "MISSING");
    JPR_INFO("Keyboard::_gameControllerId %s", gControllerId ? "found" : "MISSING");

    // This is the condition the launcher itself uses to decide whether to feed
    // the game through these objects (WindowCallbacks::WindowCallbacks). When
    // it holds, use them: it is the path the launcher exercises constantly and
    // needs no code patching.
    if (gStates && inputs && gControllerId) {
        gBackend = Backend::DirectSymbols;
        JPR_INFO("key injection ready via exported Keyboard objects (%s layout)",
                 gLegacy ? "legacy" : "modern");
        return true;
    }

    // Otherwise the launcher is using GameActivity for input, and so must we.
    JPR_INFO("Keyboard objects unavailable; trying the GameActivity path instead");
    if (gameactivity::install()) {
        gBackend = Backend::GameActivity;
        // Not ready yet: the GameActivity only exists once the game starts.
        return true;
    }

    gBackend = Backend::None;
    JPR_ERROR("KEY INJECTION UNAVAILABLE: neither the exported Keyboard objects nor");
    JPR_ERROR("GameActivity_onCreate could be used on this build. auto_jump_reset will");
    JPR_ERROR("schedule presses that go nowhere.");
    return false;
}

Backend backend() { return gBackend; }

const char* backendName(Backend value) {
    switch (value) {
    case Backend::DirectSymbols: return "exported Keyboard objects";
    case Backend::GameActivity: return "GameActivity onKeyDown";
    default: return "none";
    }
}

bool ready() {
    if (gBackend == Backend::DirectSymbols)
        return true;
    if (gBackend == Backend::GameActivity)
        return gameactivity::live();
    return false;
}

SymbolReport symbols() {
    SymbolReport report;
    report.resolved = gInitialised;
    report.states = gStates != nullptr;
    report.inputs = gInputs != nullptr || gLegacyInputs != nullptr;
    report.controllerId = gControllerId != nullptr;
    report.legacyLayout = gLegacy;
    report.gameActivityOnCreate = gameactivity::symbolFound();
    report.gameActivityHooked = gameactivity::hooked();
    report.gameActivityLive = gameactivity::live();
    return report;
}

void press(int keyCode) { queue(keyCode, true); }

void release(int keyCode) { queue(keyCode, false); }

bool holding(int keyCode) {
    if (keyCode <= 0 || keyCode > 0xff)
        return false;
    std::lock_guard<std::mutex> lock(gMutex);
    return gHeld[keyCode];
}

bool gameKeyDown(int keyCode) {
    if (keyCode <= 0 || keyCode > 0xff)
        return false;
    if (gCapture)
        return gHeld[keyCode & 0xff];
    if (gBackend == Backend::DirectSymbols && gStates)
        return gStates[keyCode & 0xff] != 0;
    // GameActivity offers no way to read key state back, but the launcher's
    // game_window API reports real presses to mods, so use that instead.
    if (gamewindow::keyStateAvailable())
        return gamewindow::keyDown(keyCode);
    return false;
}

void enableTestCapture(void (*capture)(int, bool)) {
    gCapture = capture;
    gInitialised = true;
    gBackend = capture ? Backend::DirectSymbols : Backend::None;
}

void flush() {
    if (!ready())
        return;

    std::vector<Pending> batch;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (gPending.empty())
            return;
        batch.swap(gPending);
    }

    if (gCapture) {
        for (auto const& item : batch)
            gCapture(item.key, item.down);
        return;
    }

    if (gBackend == Backend::GameActivity) {
        int sent = 0;
        for (auto const& item : batch) {
            // Modifier state is tracked locally; the mod never holds one, but
            // pass what we know so a future module that does behaves right.
            int metaState = 0;
            if (gameactivity::send(item.key, item.down, metaState))
                sent++;
        }
        status::countPressDelivered(sent, 0);
        if (logLevel() <= LogLevel::Trace) {
            for (auto const& item : batch)
                JPR_TRACE("%s %s via GameActivity", item.down ? "down" : "up", keyName(item.key));
        }
        return;
    }

    int controllerId = gControllerId ? *gControllerId : 0;
    int statesWritten = 0, eventsWritten = 0;
    for (auto const& item : batch) {
        int state = item.down ? 1 : 0;

        if (gMode != InjectMode::EventsOnly) {
            gStates[item.key & 0xff] = state;
            statesWritten++;
        }

        if (gMode == InjectMode::StatesOnly)
            continue;
        eventsWritten++;

        if (gLegacy) {
            LegacyInputEvent event{};
            event.event = state;
            event.key = (unsigned)item.key & 0xff;
            event.controllerId = controllerId;
            gLegacyInputs->push_back(event);
        } else {
            InputEvent event{};
            event.event = state;
            event.key = (unsigned)item.key & 0xff;
            event.controllerId = controllerId;
            event.modShift = gStates[16];
            event.modCtrl = gStates[17];
            event.modAlt = gStates[18];
            gInputs->push_back(event);
        }
    }

    status::countPressDelivered(eventsWritten, statesWritten);

    if (logLevel() <= LogLevel::Trace) {
        for (auto const& item : batch)
            JPR_TRACE("%s %s", item.down ? "down" : "up", keyName(item.key));
    }
}

void releaseAll() {
    std::vector<int> stuck;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        for (int key = 0; key < 256; key++) {
            if (gHeld[key])
                stuck.push_back(key);
        }
    }
    for (int key : stuck)
        release(key);
    flush();
}

}  // namespace keyboard
}  // namespace jpr
