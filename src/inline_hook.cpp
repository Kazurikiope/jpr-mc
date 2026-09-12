#define JPR_LOG_TAG "jpr.hook"

#include "jpr/inline_hook.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "jpr/api.h"
#include "jpr/log.h"
#include "inline_hook_arch.h"

namespace jpr {
namespace hook {

namespace {

struct Installed {
    uint8_t* target = nullptr;
    std::vector<uint8_t> originalBytes;
    void* trampoline = nullptr;
    size_t trampolineSize = 0;
};

std::mutex gMutex;
std::vector<Installed> gInstalled;

long pageSize() {
    static long size = sysconf(_SC_PAGESIZE);
    return size > 0 ? size : 4096;
}

// Makes [address, address+size) writable across however many pages it spans.
bool protectRange(void* address, size_t size, int protection) {
    long page = pageSize();
    auto start = (uintptr_t)address & ~(uintptr_t)(page - 1);
    auto end = ((uintptr_t)address + size + page - 1) & ~(uintptr_t)(page - 1);
    return mprotect((void*)start, end - start, protection) == 0;
}

void* allocateExecutable(size_t size) {
    void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        // Hardened kernels refuse W+X in one mapping; fall back to mapping RW
        // and flipping to RX once the trampoline is written.
        memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory == MAP_FAILED)
            return nullptr;
    }
    return memory;
}

// Writes `size` bytes into executable memory and confirms it landed.
//
// Two paths have to work here. On Linux the game's text is ordinary r-x
// memory, so it needs mprotect first. On Apple Silicon it is mapped MAP_JIT
// and mprotect to RWX is refused outright; there the launcher's
// mcpelauncher_patch is the only way in, because it wraps the write in
// pthread_jit_write_protect_np and invalidates the instruction cache.
//
// Rather than branching on a platform the mod cannot even detect — it is an
// Android ELF, so __APPLE__ is never defined here — try both and verify by
// reading the bytes back. Nothing downstream runs unless the write took.
bool writeCode(void* destination, const void* source, size_t size) {
    if (api::patch)
        api::patch(destination, source, size);
    else
        memcpy(destination, source, size);
    return memcmp(destination, source, size) == 0;
}

}  // namespace

bool install(void* target, void* replacement, void** originalOut, const char* name) {
    if (!target || !replacement) {
        JPR_ERROR("%s: refusing to hook a null address", name);
        return false;
    }

    auto* code = (uint8_t*)target;
    const size_t jumpSize = arch::jumpSize();

    // Measure whole instructions until at least the jump fits.
    std::vector<arch::Instruction> instructions;
    size_t displaced = 0;
    while (displaced < jumpSize) {
        arch::Instruction instruction;
        if (!arch::decode(code + displaced, instruction) || instruction.length == 0) {
            JPR_ERROR("%s: cannot decode the instruction at +%zu, refusing to hook", name, displaced);
            return false;
        }
        if (instruction.relativeBranch) {
            JPR_ERROR(
                "%s: a relative branch sits at +%zu, inside the %zu bytes the jump needs. "
                "Point the signature at a different instruction or pick another function.",
                name, displaced, jumpSize);
            return false;
        }
        instructions.push_back(instruction);
        displaced += instruction.length;
    }

    // Trampoline: the displaced instructions, then a jump back into the target.
    const size_t trampolineSize = displaced + jumpSize;
    auto* trampoline = (uint8_t*)allocateExecutable(trampolineSize);
    if (!trampoline) {
        JPR_ERROR("%s: could not allocate %zu bytes of executable memory", name, trampolineSize);
        return false;
    }

    size_t offset = 0;
    for (auto const& instruction : instructions) {
        if (!arch::relocate(code + offset, trampoline + offset, instruction)) {
            JPR_ERROR("%s: could not relocate the instruction at +%zu", name, offset);
            munmap(trampoline, trampolineSize);
            return false;
        }
        offset += instruction.length;
    }
    arch::writeJump(trampoline + displaced, code + displaced);

    // Flip the trampoline to RX if it was mapped RW only.
    mprotect(trampoline, trampolineSize, PROT_READ | PROT_EXEC);
    arch::flushInstructionCache(trampoline, trampolineSize);

    // Patch the target.
    Installed record;
    record.target = code;
    record.originalBytes.assign(code, code + displaced);
    record.trampoline = trampoline;
    record.trampolineSize = trampolineSize;

    // Anything past the jump is unreachable, but keeping the original bytes
    // there leaves the function disassemblable.
    std::vector<uint8_t> patchBytes(displaced);
    memcpy(patchBytes.data(), code, displaced);
    arch::writeJump(patchBytes.data(), replacement);

    // mprotect is expected to fail on Apple Silicon, where the game's text is
    // MAP_JIT; that is not an error, mcpelauncher_patch handles it.
    bool unprotected = protectRange(code, displaced, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (!unprotected && !api::patch) {
        JPR_ERROR("%s: mprotect failed on %p and the launcher patch helper is unavailable", name, target);
        munmap(trampoline, trampolineSize);
        return false;
    }

    if (!writeCode(code, patchBytes.data(), displaced)) {
        JPR_ERROR("%s: the jump did not take at %p; the page is not writable by either route", name, target);
        if (unprotected)
            protectRange(code, displaced, PROT_READ | PROT_EXEC);
        munmap(trampoline, trampolineSize);
        return false;
    }

    if (unprotected)
        protectRange(code, displaced, PROT_READ | PROT_EXEC);
    arch::flushInstructionCache(code, displaced);

    if (originalOut)
        *originalOut = trampoline;

    {
        std::lock_guard<std::mutex> lock(gMutex);
        gInstalled.push_back(std::move(record));
    }

    JPR_INFO("%s: hooked %p -> %p (%zu bytes displaced, trampoline %p)", name, target, replacement, displaced,
             (void*)trampoline);
    return true;
}

void removeAll() {
    std::vector<Installed> records;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        records.swap(gInstalled);
    }

    for (auto& record : records) {
        size_t size = record.originalBytes.size();
        bool unprotected = protectRange(record.target, size, PROT_READ | PROT_WRITE | PROT_EXEC);
        bool restored = writeCode(record.target, record.originalBytes.data(), size);
        if (unprotected)
            protectRange(record.target, size, PROT_READ | PROT_EXEC);

        if (!restored) {
            // Leaving the trampoline mapped is the lesser evil: the target
            // still jumps into it, so unmapping would turn every later call
            // into a crash.
            JPR_ERROR("could not restore the original bytes at %p; leaving the trampoline mapped",
                      (void*)record.target);
            continue;
        }

        arch::flushInstructionCache(record.target, size);
        munmap(record.trampoline, record.trampolineSize);
    }

    if (!records.empty())
        JPR_INFO("removed %zu inline hook(s)", records.size());
}

size_t installedCount() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gInstalled.size();
}

}  // namespace hook
}  // namespace jpr
