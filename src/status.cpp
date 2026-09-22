#define JPR_LOG_TAG "jpr.status"

#include "jpr/status.h"

#include <ctime>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <string>

#include "jpr/api.h"
#include "jpr/config.h"
#include "jpr/events.h"
#include "jpr/inline_hook.h"
#include "jpr/keyboard.h"
#include "jpr/log.h"
#include "jpr/module.h"
#include "jpr/signatures.h"
#include "jpr/tick.h"

namespace jpr {
namespace status {

namespace {

std::atomic<uint64_t> gTicks{0};
std::atomic<uint64_t> gHurtEvents{0};
std::atomic<uint64_t> gPressesQueued{0};
std::atomic<uint64_t> gEventsWritten{0};
std::atomic<uint64_t> gStatesWritten{0};
std::atomic<int64_t> gLastWrite{0};
time_t gLoadedAt = 0;
std::string gPhase = "not reached";

constexpr int64_t kWriteIntervalMs = 2000;

std::string timestamp(time_t when) {
    char buffer[64];
    struct tm parts {};
    localtime_r(&when, &parts);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &parts);
    return buffer;
}

}  // namespace

void markLoaded(const char* phase) {
    if (!gLoadedAt)
        gLoadedAt = time(nullptr);
    gPhase = phase;
    write();
}

void countTick() { gTicks.fetch_add(1, std::memory_order_relaxed); }
void countHurtEvent() { gHurtEvents.fetch_add(1, std::memory_order_relaxed); }
void countPressQueued() { gPressesQueued.fetch_add(1, std::memory_order_relaxed); }

void countPressDelivered(int events, int states) {
    gEventsWritten.fetch_add((uint64_t)events, std::memory_order_relaxed);
    gStatesWritten.fetch_add((uint64_t)states, std::memory_order_relaxed);
}

void writeThrottled(int64_t now) {
    int64_t last = gLastWrite.load(std::memory_order_relaxed);
    if (now - last < kWriteIntervalMs)
        return;
    gLastWrite.store(now, std::memory_order_relaxed);
    write();
}

// Sits next to jpr.json, which is the directory people are already told to
// open, rather than somewhere they have to be sent separately.
std::string statusPath() {
    const std::string& config = Config::instance().path();
    if (!config.empty()) {
        size_t slash = config.find_last_of('/');
        if (slash != std::string::npos)
            return config.substr(0, slash + 1) + "status.txt";
    }
    return std::string(api::dataDirectory()) + "status.txt";
}

void write() {
    std::string path = statusPath();
    // mod_preinit runs before the config directory is created, so make it here
    // rather than losing the earliest and most diagnostic write of all.
    ensureDirectoryFor(path);
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) {
        JPR_WARN("could not write %s", path.c_str());
        return;
    }

    time_t now = time(nullptr);
    fprintf(file, "jpr status - written %s\n", timestamp(now).c_str());
    fprintf(file, "This file is rewritten every couple of seconds while the game runs.\n");
    fprintf(file, "If it is stale or missing, the mod is not running.\n\n");

    fprintf(file, "MOD\n");
    fprintf(file, "  build              %s %s\n", __DATE__, __TIME__);
    fprintf(file, "  abi                %s\n", Signatures::abi());
    fprintf(file, "  entry point run    %s\n", gPhase.c_str());
    if (gLoadedAt) {
        fprintf(file, "  loaded at          %s\n", timestamp(gLoadedAt).c_str());
        fprintf(file, "  running for        %llds\n", (long long)(now - gLoadedAt));
    }
    fprintf(file, "  config             %s\n", Config::instance().path().c_str());

    fprintf(file, "\nLAUNCHER API\n");
    fprintf(file, "  patch helper       %s\n", api::available() ? "ok" : "MISSING");
    fprintf(file, "  game version       %s\n",
            api::version().known ? api::version().string() : "unknown");

    fprintf(file, "\nKEY INJECTION\n");
    auto symbols = keyboard::symbols();
    if (!symbols.resolved) {
        fprintf(file, "  Keyboard symbols   not looked up yet\n");
    } else {
        fprintf(file, "  Keyboard::_states           %s\n", symbols.states ? "found" : "MISSING");
        fprintf(file, "  Keyboard::_inputs           %s\n", symbols.inputs ? "found" : "MISSING");
        fprintf(file, "  Keyboard::_gameControllerId %s\n", symbols.controllerId ? "found" : "MISSING");
        fprintf(file, "  layout                      %s\n", symbols.legacyLayout ? "legacy" : "modern");
    }
    fprintf(file, "  GameActivity_onCreate       %s\n",
            symbols.gameActivityOnCreate ? "found" : "MISSING");
    if (symbols.gameActivityOnCreate) {
        fprintf(file, "  GameActivity hook           %s\n", symbols.gameActivityHooked ? "installed" : "FAILED");
        fprintf(file, "  GameActivity captured       %s\n",
                symbols.gameActivityLive ? "yes" : "not yet - starts when the game does");
    }
    fprintf(file, "  backend            %s\n", keyboard::backendName(keyboard::backend()));
    if (keyboard::ready()) {
        fprintf(file, "  status             READY\n");
    } else if (!symbols.resolved) {
        fprintf(file, "  status             NOT STARTED - mod_init did not get as far as key setup\n");
    } else {
        fprintf(file, "  status             UNAVAILABLE\n");
        if (keyboard::backend() == keyboard::Backend::GameActivity) {
            fprintf(file, "  why                The GameActivity path is hooked but the game has not\n");
            fprintf(file, "                     started yet, so there is no activity to send keys to.\n");
            fprintf(file, "                     Load a world; this should become READY.\n");
        } else {
            fprintf(file, "  why                The exported Keyboard objects are gone on this build and\n");
            fprintf(file, "                     GameActivity_onCreate could not be used either, so there\n");
            fprintf(file, "                     is no way in. No setting in jpr.json changes this.\n");
        }
    }
    fprintf(file, "  presses queued     %llu\n", (unsigned long long)gPressesQueued.load());
    fprintf(file, "  key events written %llu\n", (unsigned long long)gEventsWritten.load());
    fprintf(file, "  key states written %llu\n", (unsigned long long)gStatesWritten.load());

    fprintf(file, "\nSIGNATURES\n");
    const std::string& source = Signatures::instance().sourceDescription();
    fprintf(file, "  source             %s\n", source.empty() ? "not loaded" : source.c_str());
    fprintf(file, "  targets declared   %zu\n", Signatures::instance().targets().size());
    for (auto const& target : Signatures::instance().targets()) {
        fprintf(file, "    %-22s %s\n", target.name.c_str(),
                target.address ? "resolved" : "not resolved");
    }
    fprintf(file, "  inline hooks       %zu installed\n", hook::installedCount());

    fprintf(file, "\nMODULES\n");
    for (auto const& module : ModuleManager::instance().modules()) {
        fprintf(file, "  %-22s %s\n", module->id(), module->enabled() ? "enabled" : "disabled");
    }

    fprintf(file, "\nACTIVITY\n");
    fprintf(file, "  tick source        %s\n",
            tick::hasGameThreadSource() ? "game thread hook" : "fallback timer thread");
    fprintf(file, "  ticks              %llu\n", (unsigned long long)gTicks.load());
    fprintf(file, "  damage events seen %llu\n", (unsigned long long)gHurtEvents.load());

    fprintf(file, "\nWHAT TO CHECK\n");
    if (!symbols.resolved) {
        fprintf(file, "  Startup did not reach key setup. If this persists, the log will say why.\n");
    } else if (!keyboard::ready() && keyboard::backend() == keyboard::Backend::GameActivity) {
        fprintf(file, "  The GameActivity hook is in but the game has not started. Load a world and\n");
        fprintf(file, "  check this file again - 'GameActivity captured' should read yes.\n");
    } else if (!keyboard::ready()) {
        fprintf(file, "  Key injection is unavailable - see KEY INJECTION above. Nothing will be\n");
        fprintf(file, "  sent to the game regardless of settings.\n");
    } else if (gHurtEvents.load() == 0) {
        fprintf(file, "  No damage events have arrived. Expected while there is no local_player_hurt\n");
        fprintf(file, "  signature. Set debug_auto_fire_ms to 1500 in jpr.json to drive the module\n");
        fprintf(file, "  without one - 'damage events seen' should then climb.\n");
    } else if (gPressesQueued.load() == 0) {
        fprintf(file, "  Damage events arrive but no press was queued. Check that auto_jump_reset is\n");
        fprintf(file, "  enabled and that chance is not 0.\n");
    } else {
        fprintf(file, "  Presses are being queued and written. If the character still does not jump,\n");
        fprintf(file, "  the game is not reading these objects for jump input on this version.\n");
    }

    fclose(file);
}

}  // namespace status
}  // namespace jpr
