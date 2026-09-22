// mcpelauncher mod entry points.
//
// The loader calls mod_preinit before libminecraftpe.so is mapped and mod_init
// after. Anything that touches the game — resolving symbols, scanning for
// signatures, installing hooks — has to wait for mod_init.

#define JPR_LOG_TAG "jpr"

#include "jpr/api.h"
#include "jpr/config.h"
#include "jpr/game_hooks.h"
#include "jpr/keyboard.h"
#include "jpr/log.h"
#include "jpr/module.h"
#include "jpr/signatures.h"
#include "jpr/status.h"
#include "jpr/tick.h"

#include "game_window_api.h"

namespace {

bool gStarted = false;

void shutdown() {
    if (!gStarted)
        return;
    gStarted = false;
    jpr::tick::stop();
    jpr::keyboard::releaseAll();
    jpr::game::removeHooks();
}

}  // namespace

#define JPR_EXPORT __attribute__((visibility("default")))

extern "C" JPR_EXPORT void mod_preinit() {
    jpr::api::init();
    // Written before anything else can fail, so the file exists even if the
    // rest of startup does not survive.
    jpr::status::markLoaded("mod_preinit");
    JPR_INFO("jpr preinit (build %s, abi %s)", __DATE__, jpr::Signatures::abi());
}

extern "C" JPR_EXPORT void mod_init() {
    jpr::api::init();
    jpr::status::markLoaded("mod_init");

    jpr::Config::instance().load();
    jpr::ModuleManager::instance().applyConfig();

    if (!jpr::keyboard::init())
        JPR_ERROR("key injection is unavailable; the mod cannot do anything useful in this state");

    // Registers the per-frame callback and the keyboard callback the launcher
    // publishes for mods. The per-frame one is what moves ticking onto the
    // game thread, which is where delivering input is actually safe.
    jpr::gamewindow::init();

    jpr::Signatures::instance().load();
    int hooks = jpr::game::installHooks();

    jpr::tick::start();
    gStarted = true;

    jpr::status::write();
    JPR_INFO("jpr ready: %zu module(s), %d hook(s), config at %s",
             jpr::ModuleManager::instance().modules().size(), hooks, jpr::Config::instance().path().c_str());

    // The launcher does not offer a mod unload callback, so lean on the
    // library destructor to put the game's code back the way we found it.
    static struct Shutdown {
        ~Shutdown() { shutdown(); }
    } shutdownGuard;
}
