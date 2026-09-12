// Per-architecture pieces of the inline hooker.
#pragma once

#include <cstddef>
#include <cstdint>

namespace jpr {
namespace hook {
namespace arch {

struct Instruction {
    size_t length = 0;

    // True when the instruction encodes a target relative to its own address
    // as a branch (x86 jcc/jmp/call, aarch64 B/BL/CBZ/TBZ). Those cannot be
    // moved into a trampoline without building a veneer, so we refuse instead.
    bool relativeBranch = false;

    // True when a data operand is PC/RIP relative and needs re-basing.
    bool ripRelative = false;
    size_t displacementOffset = 0;
    size_t immediateSize = 0;
};

// Measures the instruction at `code`. Returns false when it is not something
// this decoder can account for.
bool decode(const uint8_t* code, Instruction& out);

// Bytes writeJump() emits.
size_t jumpSize();

// Writes an unconditional absolute jump to `to` at `where`.
void writeJump(uint8_t* where, const void* to);

// Copies one decoded instruction from `from` to `to`, fixing up any
// PC-relative operand. Returns false when it cannot be represented at the new
// address.
bool relocate(const uint8_t* from, uint8_t* to, const Instruction& instruction);

void flushInstructionCache(void* address, size_t size);

}  // namespace arch
}  // namespace hook
}  // namespace jpr
