// Version specific addresses, kept out of the code.
//
// Everything the mod needs to find inside libminecraftpe.so is declared in a
// JSON file under `signatures/`, picked by the running game version and ABI.
// Supporting a new Minecraft build is then a data change, not a code change.
//
// Each target may provide a `symbol`, a `pattern`, or both. The symbol is
// tried first because it survives recompiles; the pattern is the fallback for
// the (usual) case of a function with no exported name.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jpr/json.h"

namespace jpr {

struct SignatureTarget {
    std::string name;
    std::string kind;       // what the address means; see game_hooks.cpp
    std::string symbol;     // mangled exported name, optional
    std::string pattern;    // IDA style byte pattern, optional
    int occurrence = 0;     // which pattern match to take, 0 based
    int offset = 0;         // added to the match, e.g. to skip a prologue
    bool required = false;  // log at error level rather than info when missing

    // Resolved during load(). Null when the target could not be found.
    void* address = nullptr;
    std::string resolvedBy;  // "symbol" or "pattern"
};

class Signatures {
public:
    static Signatures& instance();

    // Loads the signature file matching the running version and ABI, then
    // resolves every target. Missing files and missing targets are not fatal:
    // the features that need them stay off and say so in the log.
    void load();

    // Resolved address, or null.
    void* address(const std::string& name) const;

    const SignatureTarget* target(const std::string& name) const;

    const std::vector<SignatureTarget>& targets() const { return targets_; }

    // Path the signature file was read from, or an explanation of why none was.
    const std::string& sourceDescription() const { return source_; }

    // Build time ABI name matching the launcher's ("arm64-v8a", "x86_64", ...).
    static const char* abi();

    // Overrides used by the host test build.
    void loadFromJsonForTesting(const json::Value& root);

private:
    Signatures() = default;

    void parse(const json::Value& root);
    void resolveAll();

    std::vector<SignatureTarget> targets_;
    std::string source_;
    bool resolveAddresses_ = true;
};

}  // namespace jpr
