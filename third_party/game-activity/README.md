# Vendored GameActivity headers

`android/game_activity.h`, `android/game_activity_events.h` and
`android/game_text_input.h`, copied verbatim from
[minecraft-linux/android-support-headers](https://github.com/minecraft-linux/android-support-headers),
which is what mcpelauncher itself compiles against. Copyright The Android Open
Source Project, Apache License 2.0 — see the header of each file.

They are here for one purpose: `src/game_activity_abi_check.cpp` includes them
in the Android build and `static_assert`s the layout of the structs mirrored in
`include/jpr/game_activity_abi.h`. The mod calls function pointers out of
`GameActivityCallbacks` at runtime, so a wrong offset would call the wrong
function rather than fail visibly. Checking it at compile time in CI is the
only way to be sure the mirror is right.

Nothing else includes these headers; they need `jni.h` and the `android/*`
sysroot, which the host test build does not have.
