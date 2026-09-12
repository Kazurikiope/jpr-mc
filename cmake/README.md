# Building against the Android NDK

The mod is an Android shared object: the launcher maps it with its own bionic
linker, alongside `libminecraftpe.so`. That means it has to be built with the
NDK, not with the host toolchain, even though the launcher itself is a normal
Linux/macOS program.

```
cmake -S . -B build-arm64 \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-21 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-arm64 -j
```

`ANDROID_STL=c++_shared` is not optional. The keyboard injection writes into
`Keyboard::_inputs`, a `std::vector` owned by the game, so the mod has to use
the same `libc++_shared.so` the game does. Building with `c++_static` gives the
mod its own allocator and its own vector layout, and the first push into that
vector corrupts the game's heap.

Pick the ABI that matches the APK the launcher is running, not the host CPU.
`x86_64` is the usual one on a desktop Linux install; `arm64-v8a` is what you
get on an ARM machine or when the launcher runs the ARM APK under translation.
The launcher prints the ABI it loaded at startup.
