// A self-describing status file.
//
// Asking someone to find lines in a game log has a poor success rate, and the
// three failures people report — "hits not detected", "jump not sent", "debug
// key does nothing" — all look identical from the outside whether the mod is
// broken or was never loaded at all. So the mod writes what it knows to
// `<data dir>/jpr/status.txt` and keeps it current while the game runs.
//
// Its absence is itself the most useful signal: no file means no mod, which
// means the problem is on the launcher side, not in any setting.
#pragma once

#include <cstdint>

namespace jpr {
namespace status {

// Records the first moment the mod ran, before anything can fail.
void markLoaded(const char* phase);

// Counters, incremented from the paths they describe.
void countTick();
void countHurtEvent();
void countPressQueued();
void countPressDelivered(int events, int states);

// Rewrites the file now.
void write();

// Rewrites it at most every couple of seconds. Called from the tick pump.
void writeThrottled(int64_t now);

}  // namespace status
}  // namespace jpr
