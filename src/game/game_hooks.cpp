#define JPR_LOG_TAG "jpr.game"

#include "jpr/game_hooks.h"

#include <cstdint>

#include "jpr/api.h"
#include "jpr/events.h"
#include "jpr/inline_hook.h"
#include "jpr/log.h"
#include "jpr/module.h"
#include "jpr/signatures.h"
#include "jpr/tick.h"

namespace jpr {
namespace game {

namespace {

// --- local player identity ------------------------------------------------
//
// A damage hook on Mob::hurt fires for every mob in the world, so it has to be
// filtered down to the local player. Two signature kinds supply that:
//
//   "local_player_pointer" — a global holding LocalPlayer*
//   "local_player_getter"  — a function returning LocalPlayer*
//
// When neither is configured, a hook of kind "mob_hurt" cannot tell whose
// damage it just saw and is refused, because reacting to a zombie getting hit
// would be worse than not reacting at all. A hook of kind
// "local_player_hurt" is assumed to already be player specific.

void** gLocalPlayerPointer = nullptr;
void* (*gLocalPlayerGetter)() = nullptr;

void* localPlayer() {
    if (gLocalPlayerPointer)
        return *gLocalPlayerPointer;
    if (gLocalPlayerGetter)
        return gLocalPlayerGetter();
    return nullptr;
}

bool canIdentifyLocalPlayer() { return gLocalPlayerPointer || gLocalPlayerGetter; }

// --- damage hooks ---------------------------------------------------------

// bool Mob::hurt(Mob* self, const ActorDamageSource& source, float damage,
//                bool knockback, bool ignite)
using MobHurtFn = bool (*)(void*, void*, float, bool, bool);
MobHurtFn gOriginalMobHurt = nullptr;

// The cause is the second int-sized field of ActorDamageSource on the layouts
// this has been checked against, but layouts move between versions, so it is
// only read when the signature file opts in.
bool gReadDamageCause = false;
int gDamageCauseOffset = 8;

void emit(float damage, int cause) {
    HurtEvent event;
    event.now = nowMs();
    event.damage = damage;
    event.cause = cause;
    ModuleManager::instance().dispatchLocalPlayerHurt(event);
}

bool mobHurtHook(void* self, void* source, float damage, bool knockback, bool ignite) {
    bool result = gOriginalMobHurt ? gOriginalMobHurt(self, source, damage, knockback, ignite) : false;

    // Filter first and do as little as possible on the game thread: the module
    // only records a deadline, the actual key press happens on the next tick.
    if (self && self == localPlayer() && damage > 0.0f) {
        int cause = -1;
        if (gReadDamageCause && source)
            cause = *(int*)((uintptr_t)source + gDamageCauseOffset);
        emit(damage, cause);
    }
    return result;
}

// void LocalPlayer::hurt-ish: already specific to the local player, so no
// identity check is needed. Same argument shape as Mob::hurt.
bool localPlayerHurtHook(void* self, void* source, float damage, bool knockback, bool ignite) {
    bool result = gOriginalMobHurt ? gOriginalMobHurt(self, source, damage, knockback, ignite) : false;
    if (damage > 0.0f) {
        int cause = -1;
        if (gReadDamageCause && source)
            cause = *(int*)((uintptr_t)source + gDamageCauseOffset);
        emit(damage, cause);
    }
    return result;
}

// --- game thread tick -----------------------------------------------------

void (*gOriginalGameTick)(void*) = nullptr;

void gameTickHook(void* self) {
    if (gOriginalGameTick)
        gOriginalGameTick(self);
    tick::pump(true);
}

}  // namespace

int installHooks() {
    Signatures& signatures = Signatures::instance();
    int installed = 0;

    // Local player identity, needed before any Mob::hurt style hook.
    if (void* address = signatures.address("local_player_pointer")) {
        gLocalPlayerPointer = (void**)address;
        JPR_INFO("local player pointer at %p", address);
    } else if (void* getter = signatures.address("local_player_getter")) {
        gLocalPlayerGetter = (void* (*)())getter;
        JPR_INFO("local player getter at %p", getter);
    }

    if (const SignatureTarget* cause = signatures.target("damage_cause_offset")) {
        gReadDamageCause = true;
        gDamageCauseOffset = cause->offset;
        JPR_INFO("reading ActorDamageSource cause at +%d", gDamageCauseOffset);
    }

    // Damage hook.
    const SignatureTarget* hurt = signatures.target("local_player_hurt");
    if (hurt && hurt->address) {
        if (hurt->kind == "mob_hurt" && !canIdentifyLocalPlayer()) {
            JPR_ERROR(
                "local_player_hurt is kind \"mob_hurt\" but neither local_player_pointer nor "
                "local_player_getter resolved. Without one of those every mob's damage would look like yours, "
                "so the hook is not installed.");
        } else {
            void* replacement = hurt->kind == "mob_hurt" ? (void*)&mobHurtHook : (void*)&localPlayerHurtHook;
            if (hook::install(hurt->address, replacement, (void**)&gOriginalMobHurt, "local_player_hurt"))
                installed++;
        }
    } else {
        JPR_WARN("no local_player_hurt address; auto jump reset will only fire from its debug trigger key");
    }

    // Optional per-frame tick source.
    const SignatureTarget* gameTick = signatures.target("game_tick");
    if (gameTick && gameTick->address) {
        if (hook::install(gameTick->address, (void*)&gameTickHook, (void**)&gOriginalGameTick, "game_tick")) {
            installed++;
            tick::useGameThreadSource(true);
        }
    } else {
        JPR_INFO("no game_tick address; using the fallback timer tick source");
    }

    return installed;
}

void removeHooks() {
    hook::removeAll();
    gOriginalMobHurt = nullptr;
    gOriginalGameTick = nullptr;
}

void emitSyntheticHurt() {
    HurtEvent event;
    event.now = nowMs();
    // Left at the unknown markers on purpose so a module that gates on damage
    // or cause behaves the same way it would against a real unknown event.
    ModuleManager::instance().dispatchLocalPlayerHurt(event);
}

}  // namespace game
}  // namespace jpr
