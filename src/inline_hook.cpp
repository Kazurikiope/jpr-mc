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

// Writes `size` bytes into executable memory, going through the launcher's
// patch helper when it is available so platforms with W^X are handled.
void writeCode(void* destination, const void* source, size_t size) {
    if (api::patch)
        api::patch(destination, source, size);
    else
        memcpy(destination, source, size);
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

    if (!protectRange(code, displaced, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        JPR_ERROR("%s: mprotect failed on %p", name, target);
        munmap(trampoline, trampolineSize);
        return false;
    }

    std::vector<uint8_t> patchBytes(displaced);
    // Fill the tail with the architecture's no-op-safe padding: anything past
    // the jump is unreachable, but keeping it decodable helps disassembly.
    memcpy(patchBytes.data(), code, displaced);
    arch::writeJump(patchBytes.data(), replacement);
    writeCode(code, patchBytes.data(), displaced);

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
        if (protectRange(record.target, size, PROT_READ | PROT_WRITE | PROT_EXEC)) {
            writeCode(record.target, record.originalBytes.data(), size);
            protectRange(record.target, size, PROT_READ | PROT_EXEC);
            arch::flushInstructionCache(record.target, size);
        } else {
            JPR_ERROR("could not restore the original bytes at %p", (void*)record.target);
        }
        // The trampoline is deliberately leaked when restoring fails, and
        // freed only after the target is back to its original code, so an
        // in-flight call can never land in an unmapped page.
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
