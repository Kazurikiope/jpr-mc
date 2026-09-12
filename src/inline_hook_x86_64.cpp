// x86-64 instruction length decoding and relocation for the inline hooker.
//
// Only what a function prologue can realistically contain is decoded. Anything
// outside that returns a failure so install() refuses the hook instead of
// writing a jump over an instruction it measured wrongly.
#if defined(__x86_64__)

#define JPR_LOG_TAG "jpr.hook"

#include <cstdint>
#include <cstring>

#include "jpr/log.h"
#include "inline_hook_arch.h"

namespace jpr {
namespace hook {
namespace arch {

namespace {

enum : uint8_t {
    OP_INVALID = 0,
    OP_NONE = 1,      // no modrm, no immediate
    OP_MODRM = 2,     // modrm, no immediate
    OP_MODRM_I8 = 3,  // modrm + imm8
    OP_MODRM_IZ = 4,  // modrm + imm16/32
    OP_I8 = 5,        // imm8 only
    OP_IZ = 6,        // imm16/32 only
    OP_I16 = 7,       // imm16 only
    OP_REL8 = 8,      // rel8 branch
    OP_REL32 = 9,     // rel32 branch
    OP_MOV_IMM = 10,  // B8..BF: imm64 with REX.W, else immz
    OP_GRP_F6 = 11,   // F6: imm8 only when reg field is 0 or 1
    OP_GRP_F7 = 12,   // F7: immz only when reg field is 0 or 1
};

uint8_t gPrimary[256];
uint8_t gSecondary[256];
bool gTablesReady = false;

void fill(uint8_t* table, int from, int to, uint8_t kind) {
    for (int i = from; i <= to; i++)
        table[i] = kind;
}

void buildTables() {
    if (gTablesReady)
        return;
    memset(gPrimary, OP_INVALID, sizeof(gPrimary));
    memset(gSecondary, OP_INVALID, sizeof(gSecondary));

    // 0x00..0x3F: the eight ALU groups, each laid out identically.
    for (int base = 0x00; base <= 0x38; base += 0x08) {
        fill(gPrimary, base + 0, base + 3, OP_MODRM);
        gPrimary[base + 4] = OP_I8;
        gPrimary[base + 5] = OP_IZ;
    }
    // 0x0F is the two byte escape, not an ALU opcode.
    gPrimary[0x0F] = OP_INVALID;

    fill(gPrimary, 0x50, 0x5F, OP_NONE);  // push/pop reg
    gPrimary[0x63] = OP_MODRM;            // movsxd
    gPrimary[0x68] = OP_IZ;               // push immz
    gPrimary[0x69] = OP_MODRM_IZ;         // imul r,r/m,immz
    gPrimary[0x6A] = OP_I8;               // push imm8
    gPrimary[0x6B] = OP_MODRM_I8;         // imul r,r/m,imm8
    fill(gPrimary, 0x70, 0x7F, OP_REL8);  // jcc rel8
    gPrimary[0x80] = OP_MODRM_I8;
    gPrimary[0x81] = OP_MODRM_IZ;
    gPrimary[0x83] = OP_MODRM_I8;
    fill(gPrimary, 0x84, 0x8B, OP_MODRM);  // test/xchg/mov
    gPrimary[0x8D] = OP_MODRM;             // lea
    gPrimary[0x8F] = OP_MODRM;             // pop r/m
    fill(gPrimary, 0x90, 0x97, OP_NONE);   // nop / xchg eAX,r
    gPrimary[0x98] = OP_NONE;
    gPrimary[0x99] = OP_NONE;
    gPrimary[0x9C] = OP_NONE;
    gPrimary[0x9D] = OP_NONE;
    gPrimary[0xA8] = OP_I8;
    gPrimary[0xA9] = OP_IZ;
    fill(gPrimary, 0xB0, 0xB7, OP_I8);       // mov r8, imm8
    fill(gPrimary, 0xB8, 0xBF, OP_MOV_IMM);  // mov r, imm
    gPrimary[0xC0] = OP_MODRM_I8;
    gPrimary[0xC1] = OP_MODRM_I8;
    gPrimary[0xC2] = OP_I16;  // ret imm16
    gPrimary[0xC3] = OP_NONE;
    gPrimary[0xC6] = OP_MODRM_I8;
    gPrimary[0xC7] = OP_MODRM_IZ;
    gPrimary[0xC9] = OP_NONE;  // leave
    gPrimary[0xCC] = OP_NONE;  // int3
    fill(gPrimary, 0xD0, 0xD3, OP_MODRM);
    gPrimary[0xE8] = OP_REL32;  // call rel32
    gPrimary[0xE9] = OP_REL32;  // jmp rel32
    gPrimary[0xEB] = OP_REL8;   // jmp rel8
    gPrimary[0xF4] = OP_NONE;   // hlt
    gPrimary[0xF6] = OP_GRP_F6;
    gPrimary[0xF7] = OP_GRP_F7;
    gPrimary[0xFE] = OP_MODRM;
    gPrimary[0xFF] = OP_MODRM;

    // Two byte opcodes.
    gSecondary[0x05] = OP_NONE;  // syscall
    gSecondary[0x0B] = OP_NONE;  // ud2
    fill(gSecondary, 0x10, 0x17, OP_MODRM);
    fill(gSecondary, 0x18, 0x1F, OP_MODRM);  // hints, endbr64, multi-byte nop
    fill(gSecondary, 0x28, 0x2F, OP_MODRM);
    fill(gSecondary, 0x40, 0x4F, OP_MODRM);  // cmovcc
    fill(gSecondary, 0x51, 0x6F, OP_MODRM);
    gSecondary[0x70] = OP_MODRM_I8;  // pshuf*
    fill(gSecondary, 0x71, 0x73, OP_MODRM_I8);
    fill(gSecondary, 0x74, 0x7F, OP_MODRM);
    fill(gSecondary, 0x80, 0x8F, OP_REL32);  // jcc rel32
    fill(gSecondary, 0x90, 0x9F, OP_MODRM);  // setcc
    gSecondary[0xA2] = OP_NONE;              // cpuid
    gSecondary[0xA3] = OP_MODRM;
    gSecondary[0xA4] = OP_MODRM_I8;
    gSecondary[0xA5] = OP_MODRM;
    gSecondary[0xAB] = OP_MODRM;
    gSecondary[0xAC] = OP_MODRM_I8;
    gSecondary[0xAD] = OP_MODRM;
    gSecondary[0xAE] = OP_MODRM;
    gSecondary[0xAF] = OP_MODRM;  // imul
    fill(gSecondary, 0xB0, 0xB1, OP_MODRM);
    fill(gSecondary, 0xB3, 0xB7, OP_MODRM);
    gSecondary[0xBA] = OP_MODRM_I8;
    fill(gSecondary, 0xBB, 0xBF, OP_MODRM);
    fill(gSecondary, 0xC0, 0xC1, OP_MODRM);
    gSecondary[0xC2] = OP_MODRM_I8;
    fill(gSecondary, 0xC3, 0xC6, OP_MODRM);
    fill(gSecondary, 0xD0, 0xFE, OP_MODRM);

    gTablesReady = true;
}

}  // namespace

bool decode(const uint8_t* code, Instruction& out) {
    buildTables();
    out = Instruction{};

    size_t i = 0;
    bool operandSize16 = false;
    bool rexW = false;

    // Legacy prefixes.
    for (;; i++) {
        uint8_t b = code[i];
        if (b == 0x66) {
            operandSize16 = true;
            continue;
        }
        if (b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 ||
            b == 0x64 || b == 0x65)
            continue;
        break;
    }

    // REX must be the last prefix before the opcode.
    if ((code[i] & 0xF0) == 0x40) {
        rexW = (code[i] & 0x08) != 0;
        i++;
    }

    uint8_t kind;
    uint8_t opcode = code[i];
    if (opcode == 0x0F) {
        i++;
        if (code[i] == 0x38 || code[i] == 0x3A) {
            // Three byte maps are all modrm; 0x3A additionally takes imm8.
            bool hasImm8 = code[i] == 0x3A;
            i++;
            kind = hasImm8 ? OP_MODRM_I8 : OP_MODRM;
        } else {
            kind = gSecondary[code[i]];
        }
        i++;
    } else {
        kind = gPrimary[opcode];
        i++;
    }

    if (kind == OP_INVALID)
        return false;

    if (kind == OP_REL8 || kind == OP_REL32) {
        out.relativeBranch = true;
        out.length = i + (kind == OP_REL8 ? 1 : 4);
        return true;
    }

    bool hasModRM = kind == OP_MODRM || kind == OP_MODRM_I8 || kind == OP_MODRM_IZ || kind == OP_GRP_F6 ||
                    kind == OP_GRP_F7;
    size_t immSize = 0;
    size_t immediateZ = operandSize16 ? 2 : 4;

    if (hasModRM) {
        uint8_t modrm = code[i];
        uint8_t mod = modrm >> 6;
        uint8_t reg = (modrm >> 3) & 7;
        uint8_t rm = modrm & 7;
        i++;

        if (kind == OP_GRP_F6)
            immSize = (reg == 0 || reg == 1) ? 1 : 0;
        else if (kind == OP_GRP_F7)
            immSize = (reg == 0 || reg == 1) ? immediateZ : 0;
        else if (kind == OP_MODRM_I8)
            immSize = 1;
        else if (kind == OP_MODRM_IZ)
            immSize = immediateZ;

        uint8_t sib = 0;
        bool hasSib = mod != 3 && rm == 4;
        if (hasSib) {
            sib = code[i];
            i++;
        }

        if (mod == 1) {
            i += 1;  // disp8
        } else if (mod == 2) {
            i += 4;  // disp32
        } else if (mod == 0) {
            if (rm == 5) {
                // RIP relative in 64-bit mode: the displacement has to be
                // fixed up when the instruction is copied elsewhere.
                out.ripRelative = true;
                out.displacementOffset = i;
                i += 4;
            } else if (hasSib && (sib & 7) == 5) {
                i += 4;  // SIB with no base carries a disp32
            }
        }
    } else {
        switch (kind) {
        case OP_I8: immSize = 1; break;
        case OP_I16: immSize = 2; break;
        case OP_IZ: immSize = immediateZ; break;
        case OP_MOV_IMM: immSize = rexW ? 8 : immediateZ; break;
        default: immSize = 0; break;
        }
    }

    out.length = i + immSize;
    out.immediateSize = immSize;
    return out.length > 0 && out.length <= 16;
}

size_t jumpSize() { return 14; }

void writeJump(uint8_t* where, const void* to) {
    // jmp qword ptr [rip+0]; the absolute target follows inline, so there is
    // no ±2GB range limit on where the trampoline can live.
    where[0] = 0xFF;
    where[1] = 0x25;
    where[2] = 0x00;
    where[3] = 0x00;
    where[4] = 0x00;
    where[5] = 0x00;
    uint64_t address = (uint64_t)to;
    memcpy(where + 6, &address, sizeof(address));
}

bool relocate(const uint8_t* from, uint8_t* to, const Instruction& instruction) {
    memcpy(to, from, instruction.length);
    if (!instruction.ripRelative)
        return true;

    // Re-base the displacement against the instruction's new address.
    int32_t displacement;
    memcpy(&displacement, from + instruction.displacementOffset, sizeof(displacement));

    int64_t absoluteTarget = (int64_t)(from + instruction.length) + displacement;
    int64_t adjusted = absoluteTarget - (int64_t)(to + instruction.length);
    if (adjusted > INT32_MAX || adjusted < INT32_MIN) {
        JPR_ERROR("RIP relative operand moved out of ±2GB range during relocation");
        return false;
    }

    int32_t narrowed = (int32_t)adjusted;
    memcpy(to + instruction.displacementOffset, &narrowed, sizeof(narrowed));
    return true;
}

void flushInstructionCache(void*, size_t) {
    // x86 keeps instruction and data caches coherent; nothing to do.
}

}  // namespace arch
}  // namespace hook
}  // namespace jpr

#endif  // __x86_64__
