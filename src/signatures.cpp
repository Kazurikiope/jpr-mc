#define JPR_LOG_TAG "jpr.sig"

#include "jpr/signatures.h"

#include <cstdio>
#include <cstring>

#include "jpr/api.h"
#include "jpr/log.h"
#include "jpr/sigscan.h"

namespace jpr {

namespace {

bool readFile(const std::string& path, std::string& out) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
        return false;
    char buffer[4096];
    size_t read;
    out.clear();
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
        out.append(buffer, read);
    fclose(file);
    return true;
}

}  // namespace

const char* Signatures::abi() {
#if defined(__aarch64__)
    return "arm64-v8a";
#elif defined(__arm__)
    return "armeabi-v7a";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

Signatures& Signatures::instance() {
    static Signatures signatures;
    return signatures;
}

void Signatures::load() {
    targets_.clear();

    const auto& version = api::version();
    if (!version.known) {
        source_ = "game version unknown; no signature file loaded";
        JPR_WARN("%s", source_.c_str());
        return;
    }

    // <data dir>/signatures/<version>-<abi>.json, falling back to a file
    // without the ABI suffix for versions where one file covers both.
    std::string directory = std::string(api::dataDirectory()) + "signatures/";
    std::string candidates[] = {
        directory + version.string() + "-" + abi() + ".json",
        directory + version.string() + ".json",
    };

    std::string text;
    std::string chosen;
    for (auto const& candidate : candidates) {
        if (readFile(candidate, text)) {
            chosen = candidate;
            break;
        }
    }

    if (chosen.empty()) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "no signature file for %s (%s); looked for %s", version.string(), abi(),
                 candidates[0].c_str());
        source_ = buffer;
        JPR_WARN("%s", source_.c_str());
        JPR_WARN("features that need game addresses stay off; key injection still works");
        return;
    }

    std::string error;
    json::Value root = json::parse(text, &error);
    if (!error.empty()) {
        source_ = chosen + " (invalid JSON: " + error + ")";
        JPR_ERROR("%s", source_.c_str());
        return;
    }

    source_ = chosen;
    parse(root);
    resolveAll();
}

void Signatures::loadFromJsonForTesting(const json::Value& root) {
    targets_.clear();
    source_ = "<test>";
    resolveAddresses_ = false;
    parse(root);
}

void Signatures::parse(const json::Value& root) {
    const json::Value* abiValue = root.find("abi");
    if (abiValue && abiValue->isString() && abiValue->asString() != abi()) {
        JPR_WARN("%s declares abi '%s' but this build is %s; patterns will almost certainly not match",
                 source_.c_str(), abiValue->asString().c_str(), abi());
    }

    const json::Value* targets = root.find("targets");
    if (!targets || !targets->isObject()) {
        JPR_ERROR("%s has no \"targets\" object", source_.c_str());
        return;
    }

    for (auto const& entry : targets->object()) {
        if (!entry.second.isObject()) {
            JPR_WARN("target %s is not an object, skipping", entry.first.c_str());
            continue;
        }
        SignatureTarget target;
        target.name = entry.first;

        auto readString = [&](const char* key, std::string& into) {
            const json::Value* value = entry.second.find(key);
            if (value && value->isString())
                into = value->asString();
        };
        auto readInt = [&](const char* key, int& into) {
            const json::Value* value = entry.second.find(key);
            if (value && value->isNumber())
                into = (int)value->asNumber();
        };

        readString("kind", target.kind);
        readString("symbol", target.symbol);
        readString("pattern", target.pattern);
        readInt("occurrence", target.occurrence);
        readInt("offset", target.offset);
        const json::Value* required = entry.second.find("required");
        if (required && required->isBool())
            target.required = required->asBool();

        if (target.symbol.empty() && target.pattern.empty()) {
            JPR_WARN("target %s has neither a symbol nor a pattern, skipping", target.name.c_str());
            continue;
        }
        targets_.push_back(std::move(target));
    }

    JPR_INFO("loaded %zu target(s) from %s", targets_.size(), source_.c_str());
}

void Signatures::resolveAll() {
    if (!resolveAddresses_)
        return;

    uintptr_t begin = 0, end = 0;
    bool haveRange = api::minecraftTextRange(&begin, &end);

    for (auto& target : targets_) {
        target.address = nullptr;
        target.resolvedBy.clear();

        if (!target.symbol.empty()) {
            void* found = api::minecraftSymbol(target.symbol.c_str());
            if (found) {
                target.address = (void*)((uintptr_t)found + target.offset);
                target.resolvedBy = "symbol";
                JPR_INFO("%s resolved by symbol to %p", target.name.c_str(), target.address);
                continue;
            }
            JPR_DEBUG("%s: symbol %s not exported, trying the pattern", target.name.c_str(), target.symbol.c_str());
        }

        if (target.pattern.empty())
            continue;
        if (!haveRange) {
            JPR_ERROR("%s: cannot scan, the libminecraftpe.so text range is unknown", target.name.c_str());
            continue;
        }

        sig::Pattern pattern;
        std::string error;
        if (!sig::parsePattern(target.pattern, pattern, &error)) {
            JPR_ERROR("%s: bad pattern (%s)", target.name.c_str(), error.c_str());
            continue;
        }

        void* match = sig::scan(pattern, begin, end, (size_t)target.occurrence);
        if (!match) {
            auto level = target.required ? LogLevel::Error : LogLevel::Info;
            logMessage(level, JPR_LOG_TAG, "%s: pattern did not match anywhere in libminecraftpe.so",
                       target.name.c_str());
            continue;
        }

        // An ambiguous pattern is the most common way a signature silently
        // points at the wrong function, so say so loudly.
        size_t matches = sig::countMatches(pattern, begin, end, 4);
        if (matches > 1 && target.occurrence == 0) {
            JPR_WARN("%s: pattern matches %s%zu places; using the first. Add \"occurrence\" or lengthen the pattern.",
                     target.name.c_str(), matches >= 4 ? "at least " : "", matches);
        }

        target.address = (void*)((uintptr_t)match + target.offset);
        target.resolvedBy = "pattern";
        JPR_INFO("%s resolved by pattern to %p (match %p + %d)", target.name.c_str(), target.address, match,
                 target.offset);
    }
}

void* Signatures::address(const std::string& name) const {
    const SignatureTarget* found = target(name);
    return found ? found->address : nullptr;
}

const SignatureTarget* Signatures::target(const std::string& name) const {
    for (auto const& candidate : targets_) {
        if (candidate.name == name)
            return &candidate;
    }
    return nullptr;
}

}  // namespace jpr
