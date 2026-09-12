#define JPR_LOG_TAG "jpr.keyboard"

#include "jpr/keyboard.h"

#include <cstring>
#include <mutex>
#include <vector>

#include "jpr/api.h"
#include "jpr/keycodes.h"
#include "jpr/log.h"

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
bool gReady = false;
bool gLegacy = false;
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
        return gReady;
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

    if (!gStates || !inputs) {
        JPR_ERROR("keyboard symbols missing (_states=%p _inputs=%p) — key injection unavailable", (void*)gStates,
                  inputs);
        return false;
    }
    if (!gControllerId)
        JPR_WARN("Keyboard::_gameControllerId missing; injecting with controller id 0");

    gReady = true;
    JPR_INFO("keyboard injection ready (%s layout)", gLegacy ? "legacy" : "modern");
    return true;
}

bool ready() { return gReady; }

void press(int keyCode) { queue(keyCode, true); }

void release(int keyCode) { queue(keyCode, false); }

bool holding(int keyCode) {
    if (keyCode <= 0 || keyCode > 0xff)
        return false;
    std::lock_guard<std::mutex> lock(gMutex);
    return gHeld[keyCode];
}

bool gameKeyDown(int keyCode) {
    if (!gReady || keyCode <= 0 || keyCode > 0xff)
        return false;
    if (gCapture)
        return gHeld[keyCode & 0xff];
    return gStates[keyCode & 0xff] != 0;
}

void enableTestCapture(void (*capture)(int, bool)) {
    gCapture = capture;
    gInitialised = true;
    gReady = capture != nullptr;
}

void flush() {
    if (!gReady)
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

    int controllerId = gControllerId ? *gControllerId : 0;
    for (auto const& item : batch) {
        int state = item.down ? 1 : 0;

        if (gMode != InjectMode::EventsOnly)
            gStates[item.key & 0xff] = state;

        if (gMode == InjectMode::StatesOnly)
            continue;

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
