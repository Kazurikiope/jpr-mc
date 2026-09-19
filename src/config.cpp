#define JPR_LOG_TAG "jpr.config"

#include "jpr/config.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#include "jpr/api.h"
#include "jpr/keycodes.h"
#include "jpr/log.h"

namespace jpr {

// ---------------------------------------------------------------- SettingSet

SettingSet::SettingSet(const json::Value* node, std::string modulePath)
    : node_(node && node->isObject() ? node : nullptr), path_(std::move(modulePath)) {}

const json::Value* SettingSet::lookup(const char* key) {
    claimed_.emplace_back(key);
    return node_ ? node_->find(key) : nullptr;
}

void SettingSet::warn(const char* key, const char* fmt, ...) {
    char body[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    char full[384];
    snprintf(full, sizeof(full), "%s.%s: %s", path_.c_str(), key, body);
    warnings_.emplace_back(full);
}

void SettingSet::describe(const char* key, const char* type, const char* doc, json::Value def,
                          std::vector<std::string> choices) {
    SettingInfo info;
    info.key = key;
    info.type = type;
    info.doc = doc ? doc : "";
    info.defaultValue = std::move(def);
    info.choices = std::move(choices);
    described_.push_back(std::move(info));
}

std::vector<std::string> SettingSet::unknownKeys() const {
    std::vector<std::string> unknown;
    if (!node_)
        return unknown;
    for (auto const& entry : node_->object()) {
        bool found = false;
        for (auto const& claimed : claimed_) {
            if (claimed == entry.first) {
                found = true;
                break;
            }
        }
        if (!found)
            unknown.push_back(entry.first);
    }
    return unknown;
}

void SettingSet::boolean(const char* key, bool& target, bool def, const char* doc) {
    describe(key, "bool", doc, json::Value(def));
    const json::Value* value = lookup(key);
    target = def;
    if (!value)
        return;
    if (value->isBool())
        target = value->asBool();
    else if (value->isNumber())
        target = value->asNumber() != 0.0;
    else
        warn(key, "expected true or false, keeping default %s", def ? "true" : "false");
}

void SettingSet::integer(const char* key, int& target, int def, const char* doc, int min, int max) {
    describe(key, "int", doc, json::Value(def));
    const json::Value* value = lookup(key);
    target = def;
    if (!value)
        return;
    if (!value->isNumber()) {
        warn(key, "expected a number, keeping default %d", def);
        return;
    }
    long raw = (long)value->asNumber();
    if (raw < min || raw > max) {
        warn(key, "%ld is outside %d..%d, clamping", raw, min, max);
        raw = raw < min ? min : max;
    }
    target = (int)raw;
}

void SettingSet::number(const char* key, double& target, double def, const char* doc) {
    describe(key, "number", doc, json::Value(def));
    const json::Value* value = lookup(key);
    target = def;
    if (!value)
        return;
    if (value->isNumber())
        target = value->asNumber();
    else
        warn(key, "expected a number, keeping default %g", def);
}

void SettingSet::chance(const char* key, double& target, double def, const char* doc) {
    describe(key, "chance", doc, json::Value(def));
    const json::Value* value = lookup(key);
    target = def;
    if (!value)
        return;

    double parsed = def;
    if (value->isNumber()) {
        parsed = value->asNumber();
        // A bare number above 1 can only have been meant as a percentage.
        if (parsed > 1.0)
            parsed /= 100.0;
    } else if (value->isString()) {
        std::string text = value->asString();
        char* end = nullptr;
        double raw = strtod(text.c_str(), &end);
        while (end && *end == ' ')
            end++;
        if (end && *end == '%') {
            parsed = raw / 100.0;
        } else if (end && *end == '\0') {
            parsed = raw > 1.0 ? raw / 100.0 : raw;
        } else {
            warn(key, "could not read '%s' as a probability, keeping default %g", text.c_str(), def);
            return;
        }
    } else {
        warn(key, "expected a number or percentage string, keeping default %g", def);
        return;
    }

    if (parsed < 0.0 || parsed > 1.0) {
        warn(key, "%g is outside 0..1, clamping", parsed);
        parsed = parsed < 0.0 ? 0.0 : 1.0;
    }
    target = parsed;
}

void SettingSet::range(const char* key, IntRange& target, int defMin, int defMax, const char* doc) {
    json::Array defArray{json::Value(defMin), json::Value(defMax)};
    describe(key, "range", doc, json::Value(defArray));
    const json::Value* value = lookup(key);
    target = IntRange(defMin, defMax);
    if (!value)
        return;

    if (value->isNumber()) {
        int fixed = (int)value->asNumber();
        target = IntRange(fixed, fixed);
        return;
    }
    if (!value->isArray() || value->array().size() != 2 || !value->array()[0].isNumber() ||
        !value->array()[1].isNumber()) {
        warn(key, "expected [min, max] or a single number, keeping default [%d, %d]", defMin, defMax);
        return;
    }

    int lo = (int)value->array()[0].asNumber();
    int hi = (int)value->array()[1].asNumber();
    if (hi < lo) {
        warn(key, "max %d is below min %d, swapping", hi, lo);
        std::swap(lo, hi);
    }
    if (lo < 0) {
        warn(key, "negative bound %d clamped to 0", lo);
        lo = 0;
        if (hi < 0)
            hi = 0;
    }
    target = IntRange(lo, hi);
}

void SettingSet::text(const char* key, std::string& target, const char* def, const char* doc) {
    describe(key, "string", doc, json::Value(def));
    const json::Value* value = lookup(key);
    target = def;
    if (!value)
        return;
    if (value->isString())
        target = value->asString();
    else
        warn(key, "expected a string, keeping default '%s'", def);
}

void SettingSet::key(const char* keyName, int& target, int def, const char* doc) {
    describe(keyName, "key", doc, json::Value(jpr::keyName(def)));
    const json::Value* value = lookup(keyName);
    target = def;
    if (!value)
        return;

    if (value->isNumber()) {
        target = (int)value->asNumber();
        return;
    }
    if (!value->isString()) {
        warn(keyName, "expected a key name or code, keeping default '%s'", jpr::keyName(def));
        return;
    }
    std::string text = value->asString();
    int parsed = parseKeyName(text.c_str(), -1);
    if (parsed < 0) {
        warn(keyName, "unknown key '%s', keeping default '%s'", text.c_str(), jpr::keyName(def));
        return;
    }
    target = parsed;
}

void SettingSet::choice(const char* key, std::string& target, const char* def, std::vector<std::string> choices,
                        const char* doc) {
    describe(key, "enum", doc, json::Value(def), choices);
    const json::Value* value = lookup(key);
    target = def;
    if (!value)
        return;
    if (!value->isString()) {
        warn(key, "expected one of the allowed names, keeping default '%s'", def);
        return;
    }
    std::string text = value->asString();
    for (auto const& option : choices) {
        if (option == text) {
            target = text;
            return;
        }
    }

    std::string allowed;
    for (size_t i = 0; i < choices.size(); i++) {
        if (i)
            allowed += ", ";
        allowed += choices[i];
    }
    warn(key, "'%s' is not one of [%s], keeping default '%s'", text.c_str(), allowed.c_str(), def);
}

void SettingSet::distribution(const char* key, Distribution& target, Distribution def, const char* doc) {
    std::string selected;
    choice(key, selected, distributionName(def), {"uniform", "normal"}, doc);
    target = parseDistribution(selected.c_str(), def);
}

// -------------------------------------------------------------------- Config

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

bool writeFile(const std::string& path, const std::string& content) {
    FILE* file = fopen(path.c_str(), "wb");
    if (!file)
        return false;
    bool ok = fwrite(content.data(), 1, content.size(), file) == content.size();
    fclose(file);
    return ok;
}

long modifiedTime(const std::string& path) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0)
        return 0;
    return (long)info.st_mtime;
}

}  // namespace

void ensureDirectoryFor(const std::string& path) {
    // Creates each component in turn; existing components are fine.
    for (size_t i = 1; i < path.size(); i++) {
        if (path[i] != '/')
            continue;
        std::string prefix = path.substr(0, i);
        if (mkdir(prefix.c_str(), 0755) != 0 && errno != EEXIST)
            JPR_WARN("could not create %s: %s", prefix.c_str(), strerror(errno));
    }
}

namespace {

const char* kTemplate = R"JSON({
  // jpr — configuration.
  //
  // Edited while the game is running? It reloads within a second, no restart.
  // jpr.defaults.json next to this file lists every setting with its default.
  //
  // "log_level": trace | debug | info | warn | error
  "log_level": "info",

  "modules": {
    "auto_jump_reset": {
      "enabled": false,

      // Odds of reacting at all to any given hit. 1.0 always, 0.0 never.
      "chance": 0.85,

      // Wait this long after the hit before the key goes down, in ms.
      "delay_ms": [0, 30],
      // How long the key stays held, in ms.
      "hold_ms": [90, 160],
      // Gap between repeats when "repeats" is above 1, in ms.
      "release_ms": [40, 90],

      "repeats": [1, 1],

      // Ignore further hits for this long after reacting, in ms.
      "cooldown_ms": [350, 450],

      // "normal" clusters timings around the middle of each range the way a
      // person's do; "uniform" spreads them evenly.
      "distribution": "normal",

      "key": "space"
    }
  }
}
)JSON";

}  // namespace

Config& Config::instance() {
    static Config config;
    return config;
}

void Config::load() {
    if (path_.empty())
        path_ = std::string(api::dataDirectory()) + "jpr.json";

    ensureDirectoryFor(path_);

    std::string text;
    if (!readFile(path_, text)) {
        JPR_INFO("no config at %s, writing the default one", path_.c_str());
        if (!writeFile(path_, kTemplate))
            JPR_ERROR("could not write %s: %s", path_.c_str(), strerror(errno));
        text = kTemplate;
    }

    std::string error;
    json::Value parsed = json::parse(text, &error);
    if (!error.empty()) {
        JPR_ERROR("%s is not valid JSON (%s)", path_.c_str(), error.c_str());
        if (loaded_) {
            JPR_WARN("keeping the previously loaded settings");
            return;
        }
        JPR_WARN("falling back to built-in defaults");
        parsed = json::parse(kTemplate, nullptr);
    }
    if (!parsed.isObject()) {
        JPR_ERROR("%s must contain a JSON object at the top level", path_.c_str());
        if (loaded_)
            return;
        parsed = json::parse(kTemplate, nullptr);
    }

    root_ = std::move(parsed);
    lastModified_ = modifiedTime(path_);
    loaded_ = true;

    const json::Value* level = root_.find("log_level");
    if (level && level->isString())
        setLogLevel(parseLogLevel(level->asString().c_str(), LogLevel::Info));
}

bool Config::reloadIfChanged() {
    if (!loaded_)
        return false;
    long modified = modifiedTime(path_);
    if (!modified || modified == lastModified_)
        return false;
    JPR_INFO("%s changed on disk, reloading", path_.c_str());
    load();
    return true;
}

const json::Value* Config::moduleNode(const std::string& id) const {
    const json::Value* modules = root_.find("modules");
    if (!modules)
        return nullptr;
    return modules->find(id);
}

void Config::writeReference(const json::Value& reference) const {
    if (path_.empty())
        return;
    std::string referencePath = path_;
    size_t dot = referencePath.rfind(".json");
    if (dot != std::string::npos)
        referencePath = referencePath.substr(0, dot);
    referencePath += ".defaults.json";

    if (!writeFile(referencePath, json::dump(reference)))
        JPR_WARN("could not write %s: %s", referencePath.c_str(), strerror(errno));
    else
        JPR_DEBUG("wrote settings reference to %s", referencePath.c_str());
}

}  // namespace jpr
