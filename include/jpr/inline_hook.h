// Inline (trampoline) hooking.
//
// The launcher's own hook API redirects PLT/GOT entries, which only reaches
// calls that cross a library boundary. Functions we care about — the damage
// path, a per-frame update — are called from inside libminecraftpe.so, so
// their call sites never touch a relocation. Those need the target's first
// instructions overwritten with a jump, which is what this does.
//
// The relocator is deliberately conservative: anything it cannot move
// correctly (a relative branch in the overwritten bytes, an unknown opcode)
// makes install() fail and log why, rather than half-patching a function.
#pragma once

#include <cstddef>

namespace jpr {
namespace hook {

// Redirects `target` to `replacement`.
//
// On success `*originalOut` receives a callable pointer that runs the
// displaced instructions and then continues into the untouched remainder of
// the target, so the replacement can call through to the original.
//
// `name` is only used in log messages.
bool install(void* target, void* replacement, void** originalOut, const char* name);

// Restores every installed hook. Called when the mod shuts down so the game
// is not left running patched code against freed trampolines.
void removeAll();

// Number of hooks currently installed.
size_t installedCount();

}  // namespace hook
}  // namespace jpr
