#define JPR_LOG_TAG "jpr.api"

#include "jpr/api.h"

#include <dlfcn.h>
#include <elf.h>
#include <link.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "jpr/log.h"

// Weak declarations of the launcher provided API. The mod loader satisfies
// these through ANDROID_DLEXT_MCPELAUNCHER_HOOKS; when it cannot, the weak
// symbol stays null and we notice instead of failing to load.
extern "C" {
__attribute__((weak)) void* mcpelauncher_patch(void* address, const void* data, size_t size);
__attribute__((weak)) void* mcpelauncher_hook2(void* lib, const char* sym, void* hook, void** orig);
__attribute__((weak)) void mcpelauncher_hook2_add_library(void* lib);
__attribute__((weak)) void mcpelauncher_hook2_delete(void* hook);
__attribute__((weak)) void mcpelauncher_hook2_apply();
__attribute__((weak)) extern const char mcpelauncher_package_name[];
__attribute__((weak)) extern int mcpelauncher_package_version_major;
__attribute__((weak)) extern int mcpelauncher_package_version_minor;
__attribute__((weak)) extern int mcpelauncher_package_version_patch;
__attribute__((weak)) extern int mcpelauncher_package_version_revision;
__attribute__((weak)) int __android_log_print(int priority, const char* tag, const char* fmt, ...);
}

namespace jpr {
namespace api {

void* (*patch)(void*, const void*, size_t) = nullptr;
void* (*hook2)(void*, const char*, void*, void**) = nullptr;
void (*hook2AddLibrary)(void*) = nullptr;
void (*hook2Delete)(void*) = nullptr;
void (*hook2Apply)() = nullptr;
int (*androidLog)(int, const char*, const char*, ...) = nullptr;

namespace {

bool gAvailable = false;
Version gVersion;
std::string gVersionString;
std::string gDataDirectory;
void* gMinecraft = nullptr;
bool gRangeResolved = false;
uintptr_t gTextBegin = 0;
uintptr_t gTextEnd = 0;

// Falls back to looking the symbol up in the synthetic API library when the
// weak reference was not bound by the loader.
void* lookup(void* apiLib, const char* name, void* weakValue) {
    if (weakValue)
        return weakValue;
    if (!apiLib)
        return nullptr;
    return dlsym(apiLib, name);
}

void resolveDataDirectory() {
    // The mod is loaded from `<launcher data dir>/mods/`; keep config next to
    // it in a `jpr` subdirectory so it survives launcher updates and is easy
    // for the user to find.
    Dl_info info{};
    if (dladdr((void*)&resolveDataDirectory, &info) && info.dli_fname && info.dli_fname[0]) {
        std::string path = info.dli_fname;
        size_t slash = path.find_last_of('/');
        if (slash != std::string::npos) {
            gDataDirectory = path.substr(0, slash + 1) + "jpr/";
            return;
        }
    }
    const char* home = getenv("HOME");
    gDataDirectory = home ? std::string(home) + "/.local/share/mcpelauncher/mods/jpr/" : "./jpr/";
}

}  // namespace

const char* Version::string() const { return gVersionString.c_str(); }

void init() {
    static bool done = false;
    if (done)
        return;
    done = true;

    // dlopen of the synthetic library is cheap and gives us a fallback path
    // for every symbol the weak references did not pick up.
    void* apiLib = dlopen("libmcpelauncher_mod.so", 0);

    patch = (void* (*)(void*, const void*, size_t))lookup(apiLib, "mcpelauncher_patch", (void*)mcpelauncher_patch);
    hook2 = (void* (*)(void*, const char*, void*, void**))lookup(apiLib, "mcpelauncher_hook2", (void*)mcpelauncher_hook2);
    hook2AddLibrary = (void (*)(void*))lookup(apiLib, "mcpelauncher_hook2_add_library", (void*)mcpelauncher_hook2_add_library);
    hook2Delete = (void (*)(void*))lookup(apiLib, "mcpelauncher_hook2_delete", (void*)mcpelauncher_hook2_delete);
    hook2Apply = (void (*)())lookup(apiLib, "mcpelauncher_hook2_apply", (void*)mcpelauncher_hook2_apply);
    androidLog = (int (*)(int, const char*, const char*, ...))lookup(apiLib, "__android_log_print", (void*)__android_log_print);

    int* major = &mcpelauncher_package_version_major ? &mcpelauncher_package_version_major
                                                     : (int*)(apiLib ? dlsym(apiLib, "mcpelauncher_package_version_major") : nullptr);
    int* minor = &mcpelauncher_package_version_minor ? &mcpelauncher_package_version_minor
                                                     : (int*)(apiLib ? dlsym(apiLib, "mcpelauncher_package_version_minor") : nullptr);
    int* patchV = &mcpelauncher_package_version_patch ? &mcpelauncher_package_version_patch
                                                      : (int*)(apiLib ? dlsym(apiLib, "mcpelauncher_package_version_patch") : nullptr);
    int* revision = &mcpelauncher_package_version_revision ? &mcpelauncher_package_version_revision
                                                           : (int*)(apiLib ? dlsym(apiLib, "mcpelauncher_package_version_revision") : nullptr);
    if (major && minor && patchV && revision) {
        gVersion.major = *major;
        gVersion.minor = *minor;
        gVersion.patch = *patchV;
        gVersion.revision = *revision;
        gVersion.known = true;
        char buf[64];
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d", gVersion.major, gVersion.minor, gVersion.patch, gVersion.revision);
        gVersionString = buf;
    }

    gAvailable = patch != nullptr;
    resolveDataDirectory();

    JPR_INFO("launcher api %s (patch=%p hook2=%p), minecraft %s", gAvailable ? "ready" : "MISSING",
             (void*)patch, (void*)hook2, gVersion.known ? gVersionString.c_str() : "unknown");
}

bool available() { return gAvailable; }

const Version& version() { return gVersion; }

void* minecraftHandle() {
    if (!gMinecraft)
        gMinecraft = dlopen("libminecraftpe.so", 0);
    return gMinecraft;
}

void* minecraftSymbol(const char* mangledName) {
    void* handle = minecraftHandle();
    if (!handle || !mangledName || !*mangledName)
        return nullptr;
    return dlsym(handle, mangledName);
}

bool minecraftTextRange(uintptr_t* begin, uintptr_t* end) {
    if (gRangeResolved) {
        if (!gTextEnd)
            return false;
        *begin = gTextBegin;
        *end = gTextEnd;
        return true;
    }
    gRangeResolved = true;

    // Resolve the load base from any symbol we can reach, then walk the
    // program headers of the mapped image to find the executable segment.
    // This avoids depending on dl_iterate_phdr, which the launcher's linker
    // port does not expose to loaded libraries under a stable name.
    static const char* kProbeSymbols[] = {
        "_ZN8Keyboard7_statesE",
        "_ZN8Keyboard17_gameControllerIdE",
        "_ZN5Mouse4feedEccssss",
        "bgfx_init",
    };
    void* probe = nullptr;
    for (const char* name : kProbeSymbols) {
        probe = minecraftSymbol(name);
        if (probe)
            break;
    }
    if (!probe) {
        JPR_ERROR("could not resolve any probe symbol in libminecraftpe.so");
        return false;
    }

    Dl_info info{};
    if (!dladdr(probe, &info) || !info.dli_fbase) {
        JPR_ERROR("dladdr failed for libminecraftpe.so probe symbol");
        return false;
    }

    auto base = (uintptr_t)info.dli_fbase;
    auto* header = (ElfW(Ehdr)*)base;
    if (memcmp(header->e_ident, ELFMAG, SELFMAG) != 0) {
        JPR_ERROR("libminecraftpe.so base %p is not a mapped ELF header", (void*)base);
        return false;
    }

    uintptr_t lo = 0, hi = 0;
    auto* phdr = (ElfW(Phdr)*)(base + header->e_phoff);
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD || !(phdr[i].p_flags & PF_X))
            continue;
        uintptr_t segBegin = base + phdr[i].p_vaddr;
        uintptr_t segEnd = segBegin + phdr[i].p_filesz;
        if (!hi || segBegin < lo)
            lo = segBegin;
        if (segEnd > hi)
            hi = segEnd;
    }
    if (!hi) {
        JPR_ERROR("no executable PT_LOAD segment in libminecraftpe.so");
        return false;
    }

    gTextBegin = lo;
    gTextEnd = hi;
    *begin = lo;
    *end = hi;
    JPR_INFO("libminecraftpe.so text range %p-%p (%zu KiB)", (void*)lo, (void*)hi, (size_t)((hi - lo) / 1024));
    return true;
}

const char* dataDirectory() {
    if (gDataDirectory.empty())
        resolveDataDirectory();
    return gDataDirectory.c_str();
}

}  // namespace api
}  // namespace jpr
