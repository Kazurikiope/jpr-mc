// Fallback for architectures with no instruction relocator.
//
// Only aarch64 and x86-64 have one, which covers every 64-bit ABI the launcher
// runs. The 32-bit ABIs still build and load: key injection needs no code
// patching at all, so auto jump reset works there from its debug trigger key,
// and only the signature driven hooks are unavailable.
#if !defined(__aarch64__) && !defined(__x86_64__)

#define JPR_LOG_TAG "jpr.hook"

#include <cstdint>
#include <cstring>

#include "jpr/log.h"
#include "inline_hook_arch.h"

namespace jpr {
namespace hook {
namespace arch {

bool decode(const uint8_t*, Instruction& out) {
    out = Instruction{};
    static bool warned = false;
    if (!warned) {
        warned = true;
        JPR_ERROR("inline hooking is not implemented for this architecture; signature driven hooks are disabled");
    }
    return false;
}

// Never reached: decode() always fails, so install() bails before it gets here.
size_t jumpSize() { return 16; }
void writeJump(uint8_t*, const void*) {}
bool relocate(const uint8_t*, uint8_t*, const Instruction&) { return false; }
void flushInstructionCache(void* address, size_t size) {
    __builtin___clear_cache((char*)address, (char*)address + size);
}

}  // namespace arch
}  // namespace hook
}  // namespace jpr

#endif
