# jpr

A module framework and mod for [mcpelauncher](https://minecraft-linux.github.io)
(Minecraft Bedrock on Linux/macOS), loaded from the launcher's own mods
directory. Ships with one module, **auto jump reset**.

Target version: **1.26.45.1**.

---

## Install

Drop the built library into the launcher's mods directory and put the config
next to it:

```
~/.local/share/mcpelauncher/mods/
├── libjpr.so
└── jpr/
    ├── jpr.json                      # your settings
    ├── jpr.defaults.json             # generated, read-only reference
    └── signatures/
        └── 1.26.45.1-x86_64.json     # version specific addresses
```

On macOS the mods directory is
`~/Library/Application Support/mcpelauncher/mods/` — the launcher resolves it
through `NSApplicationSupportDirectory`
(`mcpelauncher-common/src/path_helper_osx.mm`). The launcher also accepts extra
directories with `--mods dir1,dir2`.

**Pick the ABI that matches the APK, not the host CPU.** The launcher chooses
the APK ABI from its own build (`PathHelper::getAbiDir`), so an Apple Silicon
Mac runs `arm64-v8a` and a desktop Linux install usually runs `x86_64`.

### A note on Apple Silicon

The game's text pages are mapped `MAP_JIT` there, so `mprotect` to RWX is
refused and the only way to write a jump is the launcher's
`mcpelauncher_patch`, which wraps the write in `pthread_jit_write_protect_np`.
The mod cannot detect the platform — it is an Android ELF, so `__APPLE__` is
never defined in its own build — so `install()` tries both routes and then
**verifies the bytes landed by reading them back**, refusing the hook if they
did not. Key injection does not patch anything and is unaffected.

The launcher loads every `.so` in that directory at startup and calls
`mod_preinit` before `libminecraftpe.so` is mapped and `mod_init` after
(`mcpelauncher-core/src/mod_loader.cpp`). There is no separate registration
step — the file being there is the install.

`jpr.json` is created with defaults on first run, and **every module is off by
default**. Edit it and the mod reloads it within a second; no restart.

## auto_jump_reset

Holds the jump key for a short randomised window after the local player takes
damage.

| Setting | Type | Default | What it does |
| --- | --- | --- | --- |
| `enabled` | bool | `false` | Turn the module on. |
| `chance` | 0.0–1.0 | `0.85` | Odds of reacting to a given hit. `"85%"` and a bare `85` also work. |
| `delay_ms` | `[min, max]` | `[0, 30]` | Wait after the hit before the key goes down. |
| `hold_ms` | `[min, max]` | `[90, 160]` | How long the key stays held. |
| `release_ms` | `[min, max]` | `[40, 90]` | Gap between repeats. |
| `repeats` | `[min, max]` | `[1, 1]` | Presses per activation. |
| `cooldown_ms` | `[min, max]` | `[350, 450]` | Ignore further hits for this long after reacting. |
| `distribution` | `normal` \| `uniform` | `normal` | How values are drawn from the ranges. |
| `key` | key name or code | `"space"` | Key to hold. |
| `cancel_on_new_hit` | bool | `false` | Restart the sequence on a fresh hit instead of ignoring it. |
| `debug_trigger_key` | key name or code | `0` | Press to fire a fake damage event. `0` disables. |
| `inject_mode` | enum | `events_and_states` | How the key reaches the game. |

Every `[min, max]` pair accepts a bare number for a fixed value, and reversed
bounds are swapped rather than rejected.

Two details worth knowing:

* **The cooldown applies to a failed chance roll too.** Without that, a burst of
  hits would re-roll until one passed, and `0.85` would behave like `~1.0`.
* **`normal` is the default distribution.** It clusters timings around the
  middle of each range instead of spreading them flatly across it.

## Adding a module

One file, one macro. Settings bind declaratively, and the same declaration
generates `jpr.defaults.json`, so there is no config plumbing to touch:

```cpp
class MyModule : public jpr::Module {
    const char* id() const override { return "my_module"; }

    void settings(jpr::SettingSet& s) override {
        s.boolean("enabled", enabled_, false, "Turn the module on.");
        s.range("hold_ms", hold_, 90, 160, "How long to hold.");
        s.chance("chance", chance_, 0.5, "Odds of firing.");
    }

    void onLocalPlayerHurt(const jpr::HurtEvent& e) override { /* decide */ }
    void onTick(const jpr::TickContext& t) override { /* act */ }

    jpr::IntRange hold_;
    double chance_ = 0.5;
};

JPR_REGISTER_MODULE(MyModule);
```

Add the file to `JPR_CORE_SOURCES` in `CMakeLists.txt`. Registration happens
during static initialisation, so there is no central list to keep in sync.

`SettingSet` also offers `integer`, `number`, `text`, `key`, `choice` and
`distribution`. Unknown keys in a module's config section are reported by name,
so a typo shows up in the log instead of silently doing nothing.

## How it works

### Two ways in, neither needing signatures

The launcher feeds keyboard input to the game by writing two objects that
`libminecraftpe.so` exports by name (`mcpelauncher-client/src/symbols.cpp`):

* `Keyboard::_states` — the held state per key
* `Keyboard::_inputs` — the queue of press/release transitions

Both resolve with `dlsym`, so on versions that still export them the jump half
works without a single offset. That is what `inject_mode` selects between.

**1.26.45.1 does not export them** — confirmed on arm64-v8a, all three
missing. On builds like that the launcher delivers input through Android's
GameActivity instead, and so does the mod: it hooks `GameActivity_onCreate`
(exported, and already dlsym'd by the launcher) to capture the `GameActivity`
the launcher passes the game, then calls
`callbacks->onKeyDown(activity, event)` exactly as the launcher does. Still no
signature required.

The layout of those structs is mirrored in `include/jpr/game_activity_abi.h`
and `static_assert`ed against the real AOSP headers in the Android build
(`src/game_activity_abi_check.cpp`), because a wrong offset would call the
wrong function pointer rather than fail visibly.

### Detecting the hit does

Damage handling is internal to `libminecraftpe.so`. Calls to it never cross a
library boundary, so the launcher's own `mcpelauncher_hook` — which redirects
PLT/GOT entries — cannot see them. That leaves inline hooking: overwrite the
target's first instructions with a jump, and relocate what was displaced into a
trampoline.

`src/inline_hook.cpp` does that for x86-64 and aarch64. It is deliberately
conservative: if a relative branch sits inside the bytes the jump needs, or an
opcode the decoder does not account for, it refuses the hook and logs why
rather than half-patching a function.

The addresses it needs live in `signatures/<version>-<abi>.json`, never in the
code — supporting a new Minecraft build is a data change. See the comments in
that file for the shape. Nothing in it is mandatory:

| Target | Effect when missing |
| --- | --- |
| `local_player_hurt` | No damage events. `debug_trigger_key` still works. |
| `local_player_pointer` / `local_player_getter` | A `mob_hurt` kind hook is **refused** — without it, every mob's damage would look like yours. |
| `damage_cause_offset` | `HurtEvent::cause` stays `-1`. |
| `game_tick` | Falls back to the timer thread, below. |

### The tick source

All timing runs off one heartbeat. Three things can drive it, in order of
preference:

* the launcher's **per-frame callback**
  (`game_window_add_swap_buffers_callback`), which needs no signature and is
  the right answer: ticks land on the game thread, the same one the launcher
  delivers input on, at the rate the game samples input. This matters most on
  the GameActivity path, where delivering a press means calling into the
  game's input system — doing that off-thread races whatever the game thread
  is doing.
* a **game thread hook** on a `game_tick` signature, if one is configured.
* a **fallback timer thread** at 2ms. It starts unconditionally and stands
  down once a game thread source appears, since whether one will is not known
  at startup — the per-frame callback does not fire until the game renders.

`status.txt` reports which is driving.

## Diagnosing it

`status.txt` is written next to `jpr.json` and refreshed every couple of
seconds. It reports the loaded entry point, each Keyboard symbol, each
signature target, the tick source, live counters for damage events and key
writes, and a `WHAT TO CHECK` line that names the next thing to look at.

Its absence is the most informative case: no file means the mod never ran, so
the fault is on the launcher side rather than in any setting.

## Building

Host tests (no NDK needed):

```
cmake -S . -B build -DJPR_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

These cover the config binding, the randomness helpers (including that `chance`
statistically means what it says), the pattern scanner, the x86-64 instruction
decoder, and the full auto jump reset state machine against a stepped clock.

The mod itself needs the NDK — see [`cmake/README.md`](cmake/README.md). CI
builds all four ABIs and uploads a ready-to-drop-in layout as an artifact.

## Layout

```
include/jpr/     public headers
src/             core: json, config, modules, hooks, input, signatures
src/game/        signature driven hooks into libminecraftpe.so
src/modules/     one file per module
signatures/      version specific address files
config/          the jpr.json written on first run
tests/           host tests
```
