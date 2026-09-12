#pragma once

namespace jpr {
namespace game {

// Installs whatever the loaded signature file makes possible:
//
//   local_player_hurt — the damage event source
//   game_tick         — a per-frame function to drive ticks from the game
//                       thread instead of the fallback timer
//
// Returns the number of hooks installed. Missing targets are reported and
// skipped; the mod stays usable without them.
int installHooks();

void removeHooks();

// Feeds a synthetic damage event to the modules. Used by the debug trigger key
// so the timing behaviour can be tested before any signature exists.
void emitSyntheticHurt();

}  // namespace game
}  // namespace jpr
