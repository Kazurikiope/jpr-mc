#define JPR_LOG_TAG "jpr.tick"

#include "jpr/tick.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "jpr/config.h"
#include "jpr/events.h"
#include "jpr/keyboard.h"
#include "jpr/log.h"
#include "jpr/module.h"
#include "jpr/status.h"

namespace jpr {
namespace tick {

namespace {

std::atomic<bool> gRunning{false};
std::atomic<bool> gGameThreadSource{false};
std::atomic<int> gIntervalMs{2};
std::thread gThread;
std::mutex gWakeMutex;
std::condition_variable gWake;

int64_t gLastTick = 0;
int64_t gLastConfigCheck = 0;

// How often the config file's mtime is checked. Cheap, but no reason to stat
// on every single tick.
constexpr int64_t kConfigCheckIntervalMs = 500;

void timerLoop() {
    JPR_INFO("fallback timer tick source running at %dms", gIntervalMs.load());
    bool handedOver = false;
    while (gRunning.load()) {
        // The game thread source can appear at any point after startup: the
        // per-frame callback only begins firing once the game renders. Once it
        // does, stop pumping here rather than dispatching from both, which
        // would race the game thread over the key queue.
        if (gGameThreadSource.load()) {
            if (!handedOver) {
                handedOver = true;
                JPR_INFO("game thread source took over; timer no longer ticking");
            }
        } else {
            handedOver = false;
            pump(false);
        }
        std::unique_lock<std::mutex> lock(gWakeMutex);
        gWake.wait_for(lock, std::chrono::milliseconds(gIntervalMs.load()),
                       [] { return !gRunning.load(); });
    }
    JPR_INFO("fallback timer tick source stopped");
}

}  // namespace

void setInterval(int milliseconds) {
    if (milliseconds < 1)
        milliseconds = 1;
    if (milliseconds > 100)
        milliseconds = 100;
    gIntervalMs.store(milliseconds);
}

void useGameThreadSource(bool enabled) { gGameThreadSource.store(enabled); }

bool hasGameThreadSource() { return gGameThreadSource.load(); }

void start() {
    if (gRunning.exchange(true))
        return;
    // The timer thread starts regardless and stands down if and when a game
    // thread source appears, because whether one will is not known yet: the
    // per-frame callback does not fire until the game starts rendering.
    gThread = std::thread(timerLoop);
}

void stop() {
    if (!gRunning.exchange(false))
        return;
    gWake.notify_all();
    if (gThread.joinable())
        gThread.join();
    keyboard::releaseAll();
}

void pump(bool gameThread) {
    TickContext context;
    context.now = nowMs();
    context.gameThread = gameThread;
    context.deltaMs = gLastTick ? context.now - gLastTick : 0;
    gLastTick = context.now;

    if (context.now - gLastConfigCheck >= kConfigCheckIntervalMs) {
        gLastConfigCheck = context.now;
        if (Config::instance().reloadIfChanged())
            ModuleManager::instance().applyConfig();
    }

    // Hand over anything modules queued on the previous tick before they get
    // a chance to queue more, so a press and its release never collapse into
    // the same frame.
    keyboard::flush();

    status::countTick();
    ModuleManager::instance().dispatchTick(context);

    // Modules that pressed or released during this tick get their input out
    // immediately when we are on the game thread, where doing so is safe.
    if (gameThread)
        keyboard::flush();

    status::writeThrottled(context.now);
}

}  // namespace tick
}  // namespace jpr
