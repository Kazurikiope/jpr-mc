// aarch64 instruction relocation for the inline hooker.
//
// Fixed width instructions make measuring trivial; the work is recognising the
// PC-relative forms. ADR/ADRP are re-based when the new distance still fits,
// LDR-literal is turned into an absolute load, and every branch form is
// rejected so a displaced B/BL can never silently jump to the wrong place.
#if defined(__aarch64__)

#define JPR_LOG_TAG "jpr.hook"

#include <cstdint>
#include <cstring>

#include "jpr/log.h"
#include "inline_hook_arch.h"

namespace jpr {
namespace hook {
namespace arch {

namespace {

constexpr uint32_t signExtend(uint32_t value, int bits) {
    uint32_t sign = 1u << (bits - 1);
    return (value ^ sign) - sign;
}

bool isAdr(uint32_t insn) { return (insn & 0x9F000000u) == 0x10000000u; }
bool isAdrp(uint32_t insn) { return (insn & 0x9F000000u) == 0x90000000u; }
bool isLdrLiteral(uint32_t insn) { return (insn & 0x3B000000u) == 0x18000000u; }

bool isBranchImmediate(uint32_t insn) {
    if ((insn & 0x7C000000u) == 0x14000000u)  // B / BL
        return true;
    if ((insn & 0xFF000010u) == 0x54000000u)  // B.cond
        return true;
    if ((insn & 0x7E000000u) == 0x34000000u)  // CBZ / CBNZ
        return true;
    if ((insn & 0x7E000000u) == 0x36000000u)  // TBZ / TBNZ
        return true;
    return false;
}

int64_t adrOffset(uint32_t insn) {
    uint32_t immlo = (insn >> 29) & 0x3;
    uint32_t immhi = (insn >> 5) & 0x7FFFF;
    int64_t value = (int64_t)(int32_t)signExtend((immhi << 2) | immlo, 21);
    return value;
}

uint32_t setAdrOffset(uint32_t insn, int64_t offset) {
    uint32_t imm = (uint32_t)(offset & 0x1FFFFF);
    insn &= ~((0x3u << 29) | (0x7FFFFu << 5));
    insn |= (imm & 0x3u) << 29;
    insn |= ((imm >> 2) & 0x7FFFFu) << 5;
    return insn;
}

}  // namespace

bool decode(const uint8_t* code, Instruction& out) {
    uint32_t insn;
    memcpy(&insn, code, sizeof(insn));

    out = Instruction{};
    out.length = 4;
    out.relativeBranch = isBranchImmediate(insn);
    out.ripRelative = isAdr(insn) || isAdrp(insn) || isLdrLiteral(insn);
    return true;
}

size_t jumpSize() { return 16; }

void writeJump(uint8_t* where, const void* to) {
    // ldr x17, #8 ; br x17 ; <8 byte absolute target>
    //
    // x17 is the intra-procedure-call scratch register, so clobbering it at a
    // function entry is safe.
    uint32_t code[2] = {0x58000051u, 0xD61F0220u};
    memcpy(where, code, sizeof(code));
    uint64_t address = (uint64_t)to;
    memcpy(where + 8, &address, sizeof(address));
}

bool relocate(const uint8_t* from, uint8_t* to, const Instruction&) {
    uint32_t insn;
    memcpy(&insn, from, sizeof(insn));

    auto fromAddress = (int64_t)(uintptr_t)from;
    auto toAddress = (int64_t)(uintptr_t)to;

    if (isAdrp(insn)) {
        int64_t page = (fromAddress & ~0xFFFLL) + (adrOffset(insn) << 12);
        int64_t delta = (page - (toAddress & ~0xFFFLL)) >> 12;
        if (delta > 0xFFFFF || delta < -0x100000) {
            JPR_ERROR("ADRP target moved out of range during relocation");
            return false;
        }
        insn = setAdrOffset(insn, delta);
        memcpy(to, &insn, sizeof(insn));
        return true;
    }

    if (isAdr(insn)) {
        int64_t target = fromAddress + adrOffset(insn);
        int64_t delta = target - toAddress;
        if (delta > 0xFFFFF || delta < -0x100000) {
            JPR_ERROR("ADR target moved out of ±1MB range during relocation");
            return false;
        }
        insn = setAdrOffset(insn, delta);
        memcpy(to, &insn, sizeof(insn));
        return true;
    }

    if (isLdrLiteral(insn)) {
        // The literal pool entry stays where it is, so the displacement only
        // has to survive the move. 19 bits of word offset is ±1MB.
        int64_t offset = (int64_t)(int32_t)signExtend((insn >> 5) & 0x7FFFF, 19) * 4;
        int64_t target = fromAddress + offset;
        int64_t delta = target - toAddress;
        if ((delta & 3) || delta > 0xFFFFC || delta < -0x100000) {
            JPR_ERROR("LDR literal moved out of ±1MB range during relocation");
            return false;
        }
        uint32_t imm19 = (uint32_t)((delta / 4) & 0x7FFFF);
        insn = (insn & ~(0x7FFFFu << 5)) | (imm19 << 5);
        memcpy(to, &insn, sizeof(insn));
        return true;
    }

    memcpy(to, from, 4);
    return true;
}

void flushInstructionCache(void* address, size_t size) {
    __builtin___clear_cache((char*)address, (char*)address + size);
}

}  // namespace arch
}  // namespace hook
}  // namespace jpr

#endif  // __aarch64__
