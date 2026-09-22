#pragma once

#include "jpr/game_activity_abi.h"

namespace jpr {
namespace keyboard {
namespace gameactivity {

// Hooks GameActivity_onCreate so the GameActivity can be captured when the
// game starts. Returns whether the hook went in; injection only becomes usable
// once live() is true, which happens later, when the game actually starts.
bool install();

// True once onCreate has run and handed us a usable callbacks table.
bool live();

bool symbolFound();
bool hooked();

// Delivers one press or release. Returns false when it could not be sent.
bool send(int minecraftKey, bool down, int metaState);

}  // namespace gameactivity
}  // namespace keyboard
}  // namespace jpr
