// Auto jump reset.
//
// When the local player takes damage, hold the jump key for a short randomised
// window. Everything about that — whether it fires at all, how long it waits,
// how long it holds, how often it repeats — comes from the config.
//
// The work is driven entirely from onTick(): onLocalPlayerHurt() only decides
// *whether* to act and writes down a schedule. That keeps the damage hook,
// which runs on the game thread inside the game's own call stack, down to a
// few comparisons.

#define JPR_LOG_TAG "jpr.autojumpreset"

#include "jpr/game_hooks.h"
#include "jpr/keyboard.h"
#include "jpr/keycodes.h"
#include "jpr/log.h"
#include "jpr/module.h"
#include "jpr/random.h"

namespace jpr {
namespace {

class AutoJumpReset : public Module {
public:
    const char* id() const override { return "auto_jump_reset"; }

    const char* description() const override {
        return "Holds the jump key for a randomised window after the local player takes damage.";
    }

    bool wantsHurtEvents() const override { return true; }

    void settings(SettingSet& set) override {
        set.boolean("enabled", enabled_, false, "Turn the module on.");

        set.chance("chance", chance_, 0.85,
                   "Odds of reacting to any given hit. 1.0 always, 0.0 never. "
                   "Percentages ('85%' or 85) are accepted too.");

        set.range("delay_ms", delay_, 0, 30, "Wait this long after the hit before the key goes down.");
        set.range("hold_ms", hold_, 90, 160, "How long the key stays held.");
        set.range("release_ms", release_, 40, 90, "Gap between repeats. Only used when repeats is above 1.");
        set.range("repeats", repeats_, 1, 1, "How many times to press per activation.");
        set.range("cooldown_ms", cooldown_, 350, 450, "Ignore further hits for this long after reacting.");

        set.distribution("distribution", distribution_, Distribution::Normal,
                         "How values are drawn from the ranges above. 'normal' clusters them around the "
                         "middle of each range the way a person's timings do; 'uniform' spreads them evenly.");

        set.key("key", key_, keycode::Space, "Key to hold. A name ('space', 'w', 'f5') or a raw code.");

        set.boolean("cancel_on_new_hit", cancelOnNewHit_, false,
                    "When a fresh hit lands mid-sequence, restart the sequence instead of ignoring the hit. "
                    "Only reachable when cooldown_ms is short enough to allow it.");

        set.key("debug_trigger_key", debugKey_, 0,
                "Pressing this key fires a fake damage event, so the timing can be tested without a "
                "working damage hook. 0 disables it. This reads the game's key state, so it needs the "
                "same Keyboard symbols injection does — if those are missing, use debug_auto_fire_ms "
                "instead.");

        set.integer("debug_auto_fire_ms", debugAutoFireMs_, 0,
                    "Fire a fake damage event every this many milliseconds. 0 disables it. Unlike "
                    "debug_trigger_key this reads nothing from the game, so it still runs when key "
                    "symbols are missing — which makes it the way to tell 'the mod is not firing' apart "
                    "from 'the mod is firing but the key is not arriving'.",
                    0, 600000);

        std::string injectMode;
        set.choice("inject_mode", injectMode, "events_and_states", {"events_and_states", "states_only", "events_only"},
                   "How the key reaches the game. 'events_and_states' matches what the launcher does for a "
                   "real key press and is what you want.");
        injectMode_ = keyboard::parseInjectMode(injectMode.c_str(), keyboard::InjectMode::EventsAndStates);
    }

    void onConfigured(bool enabledChanged) override {
        keyboard::setInjectMode(injectMode_);
        if (enabledChanged && !enabled_)
            abortSequence("module disabled");

        // A key change mid-press would otherwise leave the old key held.
        if (active_ && heldKey_ != key_)
            abortSequence("jump key changed");
    }

    void onLocalPlayerHurt(const HurtEvent& event) override {
        if (!enabled_)
            return;

        // Deliberately not gated on keyboard::ready(). When injection is
        // unavailable the module still runs its whole decision path and says
        // so once, because "nothing happened" is indistinguishable from a
        // failed chance roll otherwise.
        if (!keyboard::ready() && !warnedNoKeyboard_) {
            warnedNoKeyboard_ = true;
            JPR_ERROR("reacting to hits, but key injection is unavailable — see the startup log. "
                      "Presses are being scheduled and dropped.");
        }

        if (event.now < cooldownUntil_) {
            JPR_DEBUG("hit ignored, %lldms of cooldown left", (long long)(cooldownUntil_ - event.now));
            return;
        }

        if (active_ && !cancelOnNewHit_) {
            JPR_DEBUG("hit ignored, a sequence is already running");
            return;
        }

        Random& random = Random::instance();
        if (!random.chance(chance_)) {
            // The cooldown still applies to a failed roll. Without that, a
            // burst of hits would re-roll until one passed, which turns
            // "85% of the time" into "almost always".
            cooldownUntil_ = event.now + random.pick(cooldown_, distribution_);
            JPR_DEBUG("chance roll failed, skipping this hit");
            return;
        }

        if (active_)
            abortSequence("restarting for a new hit");

        active_ = true;
        pressesLeft_ = random.pick(repeats_, distribution_);
        if (pressesLeft_ < 1)
            pressesLeft_ = 1;
        heldKey_ = key_;
        holding_ = false;
        nextActionAt_ = event.now + random.pick(delay_, distribution_);

        JPR_DEBUG("reacting to a hit: %d press(es), first in %lldms", pressesLeft_,
                  (long long)(nextActionAt_ - event.now));
    }

    void onTick(const TickContext& tick) override {
        pollDebugKey(tick);

        if (!active_)
            return;
        if (tick.now < nextActionAt_)
            return;

        Random& random = Random::instance();

        if (!holding_) {
            keyboard::press(heldKey_);
            JPR_DEBUG("press %s", keyName(heldKey_));
            holding_ = true;
            nextActionAt_ = tick.now + random.pick(hold_, distribution_);
            return;
        }

        keyboard::release(heldKey_);
        holding_ = false;
        pressesLeft_--;

        if (pressesLeft_ > 0) {
            nextActionAt_ = tick.now + random.pick(release_, distribution_);
            return;
        }

        active_ = false;
        cooldownUntil_ = tick.now + random.pick(cooldown_, distribution_);
        JPR_DEBUG("sequence done, cooldown for %lldms", (long long)(cooldownUntil_ - tick.now));
    }

private:
    // Watches the debug trigger key so the press/hold/release behaviour can be
    // exercised without a damage hook. Edge triggered, so holding the key down
    // fires once.
    void pollDebugKey(const TickContext& tick) {
        if (!enabled_)
            return;

        if (debugAutoFireMs_ > 0 && tick.now - lastAutoFire_ >= debugAutoFireMs_) {
            lastAutoFire_ = tick.now;
            JPR_INFO("debug_auto_fire_ms elapsed, firing a synthetic hit");
            game::emitSyntheticHurt();
        }

        if (debugKey_ <= 0)
            return;
        bool down = keyboard::gameKeyDown(debugKey_);
        if (down && !debugKeyWasDown_) {
            JPR_INFO("debug trigger key pressed, firing a synthetic hit");
            game::emitSyntheticHurt();
        }
        debugKeyWasDown_ = down;
    }

    void abortSequence(const char* why) {
        if (!active_ && !holding_)
            return;
        if (holding_)
            keyboard::release(heldKey_);
        active_ = false;
        holding_ = false;
        pressesLeft_ = 0;
        JPR_DEBUG("sequence aborted: %s", why);
    }

    // Settings.
    double chance_ = 0.85;
    IntRange delay_{0, 30};
    IntRange hold_{90, 160};
    IntRange release_{40, 90};
    IntRange repeats_{1, 1};
    IntRange cooldown_{350, 450};
    Distribution distribution_ = Distribution::Normal;
    int key_ = keycode::Space;
    bool cancelOnNewHit_ = false;
    int debugKey_ = 0;
    int debugAutoFireMs_ = 0;
    keyboard::InjectMode injectMode_ = keyboard::InjectMode::EventsAndStates;

    // Sequence state.
    bool active_ = false;
    bool holding_ = false;
    int heldKey_ = keycode::Space;
    int pressesLeft_ = 0;
    int64_t nextActionAt_ = 0;
    int64_t cooldownUntil_ = 0;
    bool debugKeyWasDown_ = false;
    bool warnedNoKeyboard_ = false;
    int64_t lastAutoFire_ = 0;
};

JPR_REGISTER_MODULE(AutoJumpReset);

}  // namespace
}  // namespace jpr
