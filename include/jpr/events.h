#pragma once

#include <cstdint>

namespace jpr {

// Milliseconds on a steady clock, shared by every timing decision in the mod.
int64_t nowMs();

// Replaces the clock. Tests use this to step time deterministically; pass
// nullptr to go back to the real steady clock.
void setClockForTesting(int64_t (*clock)());

struct TickContext {
    int64_t now = 0;       // nowMs() at the start of this tick
    int64_t deltaMs = 0;   // since the previous tick
    bool gameThread = false;  // false when driven by the fallback timer thread
};

// Emitted when the local player takes damage.
//
// Only `now` is always populated. The rest is filled in from whatever the
// resolved damage hook gives us, and stays at its unknown marker when the
// signature for that version does not expose it.
struct HurtEvent {
    int64_t now = 0;

    // Damage in half-hearts, or -1 when unknown.
    float damage = -1.0f;

    // ActorDamageCause as the game numbers it, or -1 when unknown.
    int cause = -1;

    // 1 yes, 0 no, -1 unknown.
    int attackerIsPlayer = -1;

    bool damageKnown() const { return damage >= 0.0f; }
};

}  // namespace jpr
