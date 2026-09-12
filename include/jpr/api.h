// Thin binding layer over the mcpelauncher mod API.
//
// mcpelauncher exposes its API to mods through a synthetic library called
// `libmcpelauncher_mod.so`, whose symbols the mod loader injects when it
// dlopen()s a mod (see mcpelauncher-core/src/minecraft_utils.cpp,
// MinecraftUtils::getApi). We declare those entry points weak so that the mod
// still links and loads when a symbol is missing, then probe for them at
// startup instead of crashing on an unresolved reference.
#pragma once

#include <cstddef>
#include <cstdint>

namespace jpr {
namespace api {

// --- Resolved launcher entry points (null when unavailable) ---------------

// memcpy into executable memory, handling W^X on platforms that need it.
extern void* (*patch)(void* address, const void* data, size_t size);

// Relocation based hooking. Only works for symbols reached through the PLT/GOT,
// so it cannot redirect calls internal to libminecraftpe.so. We use it where it
// applies and fall back to inline hooks elsewhere.
extern void* (*hook2)(void* lib, const char* symbol, void* replacement, void** original);
extern void (*hook2AddLibrary)(void* lib);
extern void (*hook2Delete)(void* hook);
extern void (*hook2Apply)();

// android liblog; the launcher routes this into its own logger.
extern int (*androidLog)(int priority, const char* tag, const char* fmt, ...);

// Minecraft package version, as parsed by the launcher from the APK manifest.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    int revision = 0;
    bool known = false;

    // "1.26.45.1" style. Empty when `known` is false.
    const char* string() const;
};

// Must be called once from mod_preinit/mod_init before anything else.
void init();

// True once init() found the launcher API. A mod loaded outside mcpelauncher
// (host tests, for example) will see false and should degrade gracefully.
bool available();

const Version& version();

// Handle of libminecraftpe.so, or null before the game library is loaded.
void* minecraftHandle();

// Address of a mangled symbol exported by libminecraftpe.so, or null.
void* minecraftSymbol(const char* mangledName);

// Executable range of libminecraftpe.so, used for signature scanning.
// Returns false when the range could not be determined.
bool minecraftTextRange(uintptr_t* begin, uintptr_t* end);

// Directory the mod keeps its config in
// (`<launcher data dir>/mods/jpr/`, resolved relative to the loaded mod).
const char* dataDirectory();

}  // namespace api
}  // namespace jpr
