// Declarative settings binding.
//
// A module describes its settings once, in one line each:
//
//     void settings(SettingSet& s) override {
//         s.boolean("enabled", enabled, false, "turn the module on");
//         s.chance ("chance", chance, 0.85, "odds of reacting to a hit");
//         s.range  ("hold_ms", holdMs, 90, 160, "how long the key is held");
//     }
//
// The same description drives three things: reading jpr.json into the
// module's members, validating what it finds, and generating the
// jpr.defaults.json reference file. Adding a setting therefore never means
// touching the config plumbing.
#pragma once

#include <string>
#include <vector>

#include "jpr/json.h"
#include "jpr/random.h"

namespace jpr {

// One described setting, kept for the generated reference file.
struct SettingInfo {
    std::string key;
    std::string type;  // "bool", "int", "number", "chance", "range", "string", "key", "enum"
    std::string doc;
    json::Value defaultValue;
    std::vector<std::string> choices;  // for "enum"
};

class SettingSet {
public:
    // `node` is the module's object in jpr.json, or null when absent.
    explicit SettingSet(const json::Value* node, std::string modulePath);

    void boolean(const char* key, bool& target, bool def, const char* doc = "");
    void integer(const char* key, int& target, int def, const char* doc = "", int min = INT_MIN_SENTINEL, int max = INT_MAX_SENTINEL);
    void number(const char* key, double& target, double def, const char* doc = "");

    // A probability. Accepts 0.0 .. 1.0, and also "85%" or 85 for convenience,
    // because writing a percentage is the thing people reach for first.
    void chance(const char* key, double& target, double def, const char* doc = "");

    // An inclusive [min, max] pair, written as `[90, 160]`. A bare number is
    // accepted too and means a fixed value.
    void range(const char* key, IntRange& target, int defMin, int defMax, const char* doc = "");

    void text(const char* key, std::string& target, const char* def, const char* doc = "");

    // A key name ("space", "w", "f5") or raw code.
    void key(const char* keyName, int& target, int def, const char* doc = "");

    // One of `choices`; falls back to `def` and warns on anything else.
    void choice(const char* key, std::string& target, const char* def, std::vector<std::string> choices, const char* doc = "");

    void distribution(const char* key, Distribution& target, Distribution def, const char* doc = "");

    const std::vector<SettingInfo>& described() const { return described_; }

    // Warnings collected while reading (unknown keys, out of range values).
    const std::vector<std::string>& warnings() const { return warnings_; }

    // Keys present in the config that no setting claimed. Reported so typos
    // like `hold_ms_` are visible instead of silently doing nothing.
    std::vector<std::string> unknownKeys() const;

    static constexpr int INT_MIN_SENTINEL = -2147483647 - 1;
    static constexpr int INT_MAX_SENTINEL = 2147483647;

private:
    const json::Value* node_;
    std::string path_;
    std::vector<SettingInfo> described_;
    std::vector<std::string> warnings_;
    std::vector<std::string> claimed_;

    const json::Value* lookup(const char* key);
    void warn(const char* key, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    void describe(const char* key, const char* type, const char* doc, json::Value def, std::vector<std::string> choices = {});
};

// Loads, watches and reloads the mod's config file.
class Config {
public:
    static Config& instance();

    // Reads the config from disk, creating it from the shipped template on
    // first run. Safe to call repeatedly.
    void load();

    // Re-reads when the file changed on disk. Called from the tick source, so
    // edits take effect without restarting the game.
    // Returns true when a reload happened.
    bool reloadIfChanged();

    const json::Value& root() const { return root_; }

    // Module subtree of `modules`, or null.
    const json::Value* moduleNode(const std::string& id) const;

    const std::string& path() const { return path_; }

    // Writes jpr.defaults.json: every module, every setting, its type,
    // documentation and default. Regenerated on every load so it always
    // matches the build that produced it.
    void writeReference(const json::Value& reference) const;

    void setPathForTesting(std::string path) { path_ = std::move(path); }

private:
    Config() = default;

    std::string path_;
    json::Value root_;
    long lastModified_ = 0;
    bool loaded_ = false;
};

}  // namespace jpr
