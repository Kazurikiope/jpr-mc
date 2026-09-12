# Setting up jpr on macOS (Apple Silicon)

Everything below assumes an Apple Silicon Mac (M1/M2/M3/M4) running
mcpelauncher with Minecraft Bedrock **1.26.45.1**.

**The ABI you need is `arm64-v8a`.** The launcher picks the APK ABI from its own
build, not from your CPU's capabilities (`PathHelper::getAbiDir`), so an Apple
Silicon Mac runs the `arm64-v8a` APK. An `x86_64` build of the mod will not
load.

---

## 1. Get `libjpr.so`

### Option A — download it (no toolchain needed)

1. Go to the repo's **Actions** tab → the newest green `build` run.
2. Under **Artifacts**, download **`jpr-arm64-v8a`**.
3. Unzip it. You get `libjpr.so` plus a `jpr/` folder with the config and
   signature templates.
4. Strip the quarantine flag macOS puts on anything downloaded:

   ```sh
   xattr -dr com.apple.quarantine ~/Downloads/jpr-arm64-v8a
   ```

### Option B — build it yourself

The mod is an **Android** shared object — the launcher maps it with its own
bionic linker, alongside `libminecraftpe.so`. So it needs the Android NDK, not
Xcode's clang, even though you are building on a Mac.

```sh
brew install cmake
brew install --cask android-ndk     # or Android Studio → SDK Manager → NDK

# Wherever the NDK landed. Check with: ls ~/Library/Android/sdk/ndk
export ANDROID_NDK_HOME=~/Library/Android/sdk/ndk/27.2.12479018

git clone https://github.com/Kazurikiope/jpr-mc
cd jpr-mc
git checkout claude/minecraft-autojump-mod-0ilbbs

cmake -S . -B build-arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-21 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm64 -j
```

You get `build-arm64/libjpr.so`.

> `ANDROID_STL=c++_shared` is **not optional**. The key injection writes into
> `Keyboard::_inputs`, a `std::vector` owned by the game, so the mod has to use
> the same `libc++_shared.so` the game does. `c++_static` gives the mod its own
> allocator and its own vector layout, and the first push corrupts the game's
> heap.

## 2. Install

```sh
MODS="$HOME/Library/Application Support/mcpelauncher/mods"
mkdir -p "$MODS/jpr/signatures"
cp build-arm64/libjpr.so "$MODS/"          # or the downloaded one
```

Final layout:

```
~/Library/Application Support/mcpelauncher/mods/
├── libjpr.so
└── jpr/
    ├── jpr.json                       # created on first run
    ├── jpr.defaults.json              # generated reference, read-only
    └── signatures/
        └── 1.26.45.1-arm64-v8a.json   # optional, see step 5
```

There is no registration step — the launcher loads every `.so` in that
directory at startup and calls the mod's entry points.

## 3. First run

Launch the game once. The mod creates `jpr/jpr.json` with defaults and
regenerates `jpr/jpr.defaults.json`, which lists every setting this build reads
with its type, default and a one-line explanation.

To see the mod's log, start the launcher from Terminal:

```sh
/Applications/mcpelauncher-ui-qt.app/Contents/MacOS/mcpelauncher-ui-qt
```

You want a line like:

```
[INFO/jpr] jpr ready: 1 module(s), 0 hook(s), config at .../jpr/jpr.json
[INFO/jpr.keyboard] keyboard injection ready (modern layout)
```

`0 hook(s)` is expected right now — see step 5.

## 4. Configure

Open `jpr/jpr.json`. **Everything is off by default**, so the first edit is
`"enabled": true`.

```jsonc
{
  "log_level": "info",

  "modules": {
    "auto_jump_reset": {
      "enabled": true,

      // React to 85% of hits. "85%" and a bare 85 also work.
      "chance": 0.85,

      // Every timing is [min, max] in milliseconds, drawn fresh each time.
      // A bare number instead of a pair means a fixed value.
      "delay_ms":    [0, 30],      // wait after the hit before the key goes down
      "hold_ms":     [90, 160],    // how long the key stays held
      "release_ms":  [40, 90],     // gap between repeats
      "repeats":     [1, 1],       // presses per activation
      "cooldown_ms": [350, 450],   // ignore further hits for this long

      // "normal" clusters timings around the middle of each range the way a
      // person's do. "uniform" spreads them evenly.
      "distribution": "normal",

      "key": "space"
    }
  }
}
```

**Edits apply within a second — no restart.** Save the file and the mod
reloads it. Typos are reported by name in the log rather than silently
ignored, e.g. `modules.auto_jump_reset.hold_msx is not a setting this build
knows about`.

### The settings in full

| Setting | Type | Default | What it does |
| --- | --- | --- | --- |
| `enabled` | bool | `false` | Turn the module on. |
| `chance` | 0.0–1.0 | `0.85` | Odds of reacting to a given hit. |
| `delay_ms` | `[min, max]` | `[0, 30]` | Wait after the hit before pressing. |
| `hold_ms` | `[min, max]` | `[90, 160]` | How long the key stays held. |
| `release_ms` | `[min, max]` | `[40, 90]` | Gap between repeats. |
| `repeats` | `[min, max]` | `[1, 1]` | Presses per activation. |
| `cooldown_ms` | `[min, max]` | `[350, 450]` | Ignore further hits for this long. |
| `distribution` | `normal` \| `uniform` | `normal` | How values are drawn from the ranges. |
| `key` | key name or code | `"space"` | Key to hold. `"space"`, `"w"`, `"f5"`, `32`. |
| `cancel_on_new_hit` | bool | `false` | Restart on a fresh hit instead of ignoring it. |
| `debug_trigger_key` | key name or code | `0` | Press to fire a fake hit. `0` disables. |
| `inject_mode` | enum | `events_and_states` | How the key reaches the game. |

Two behaviours worth knowing before you tune anything:

* **The cooldown applies to a failed chance roll too.** Otherwise a burst of
  hits would re-roll until one passed, and `0.85` would behave like `1.0`.
* **Reversed bounds are swapped, not rejected.** `[160, 90]` means the same as
  `[90, 160]`.

### Tuning examples

```jsonc
// Always react, tight and fast
"chance": 1.0, "delay_ms": 0, "hold_ms": [70, 100], "cooldown_ms": [250, 300]

// Occasional and lazy
"chance": "40%", "delay_ms": [40, 120], "hold_ms": [120, 220]

// Double tap
"repeats": [2, 2], "hold_ms": [80, 110], "release_ms": [50, 80]

// Sometimes one press, sometimes two
"repeats": [1, 2]
```

## 5. Testing it right now

**The damage detection is not wired up yet** — it needs signatures for
1.26.45.1 that have not been provided. `signatures/1.26.45.1-arm64-v8a.json` is
a template with empty `pattern` fields, so the mod will not react to real hits.

Everything else works, and you can test the whole press/hold/release behaviour
today with the debug trigger key:

```jsonc
"debug_trigger_key": "f8"
```

Press **F8** in game. That fires a synthetic damage event, which runs exactly
the same path a real hit would: the chance roll, the delay, the hold, the
repeats, the cooldown. Set `"log_level": "debug"` to watch each decision:

```
[DEBUG/jpr.autojumpreset] reacting to a hit: 1 press(es), first in 12ms
[DEBUG/jpr.autojumpreset] sequence done, cooldown for 412ms
```

That is enough to dial in the timing values. When the signatures land, the only
thing that changes is *what* triggers the sequence.

## Troubleshooting

**No `[INFO/jpr]` lines at all.** The `.so` is not being loaded. Check it is
directly in `mods/` (not in a subfolder), that it is the `arm64-v8a` build, and
that quarantine is cleared.

**`keyboard symbols missing`.** The mod could not find `Keyboard::_states` /
`Keyboard::_inputs` in `libminecraftpe.so`. That means a version mismatch —
report the version the log prints.

**`the jump did not take at 0x...`.** Only appears once signatures exist. On
Apple Silicon the game's text is mapped `MAP_JIT`, so writing a hook goes
through the launcher's patch helper rather than `mprotect`; the mod verifies
the write landed and refuses the hook if it did not, rather than half-patching
a function. This path is written but **has not been tested on real hardware**.
