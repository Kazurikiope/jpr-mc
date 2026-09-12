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
#include "jpr/tick.h"

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
    JPR_INFO("jpr preinit (build %s, abi %s)", __DATE__, jpr::Signatures::abi());
}

extern "C" JPR_EXPORT void mod_init() {
    jpr::api::init();

    jpr::Config::instance().load();
    jpr::ModuleManager::instance().applyConfig();

    if (!jpr::keyboard::init())
        JPR_ERROR("key injection is unavailable; the mod cannot do anything useful in this state");

    jpr::Signatures::instance().load();
    int hooks = jpr::game::installHooks();

    jpr::tick::start();
    gStarted = true;

    JPR_INFO("jpr ready: %zu module(s), %d hook(s), config at %s",
             jpr::ModuleManager::instance().modules().size(), hooks, jpr::Config::instance().path().c_str());

    // The launcher does not offer a mod unload callback, so lean on the
    // library destructor to put the game's code back the way we found it.
    static struct Shutdown {
        ~Shutdown() { shutdown(); }
    } shutdownGuard;
}
