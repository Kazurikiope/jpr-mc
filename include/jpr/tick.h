// The mod's heartbeat.
//
// Everything time based — holding a key for 120ms, expiring a cooldown,
// noticing that jpr.json changed — is driven from here, so there is exactly
// one place that decides when work happens.
//
// Two sources can drive it:
//
//   * a game thread hook, installed when the signature file names a per-frame
//     function. This is the good one: ticks land on the same thread the game
//     reads input on, so queued key events are handed over safely.
//   * a fallback timer thread, used when no such signature is configured.
//     Timing is still accurate, but the key queue is handed to the game from
//     off-thread. See `input.inject_mode` if that ever causes trouble.
#pragma once

namespace jpr {
namespace tick {

// Interval for the fallback timer thread, in milliseconds.
void setInterval(int milliseconds);

// Marks that a game thread source is driving ticks. When set before start(),
// the timer thread is not launched at all.
void useGameThreadSource(bool enabled);
bool hasGameThreadSource();

// Starts whichever source applies. Idempotent.
void start();
void stop();

// Runs one tick: reloads config if it changed, flushes queued input and
// dispatches to modules.
void pump(bool gameThread);

}  // namespace tick
}  // namespace jpr
