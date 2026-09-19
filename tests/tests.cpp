// Host side tests.
//
// These exercise everything that does not need libminecraftpe.so: the config
// binding, the randomness helpers, the pattern scanner, the x86-64 instruction
// decoder and — through the keyboard capture hook — the full auto jump reset
// state machine against a stepped clock.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "jpr/config.h"
#include "jpr/events.h"
#include "jpr/game_hooks.h"
#include "jpr/json.h"
#include "jpr/keyboard.h"
#include "jpr/keycodes.h"
#include "jpr/log.h"
#include "jpr/module.h"
#include "jpr/random.h"
#include "jpr/sigscan.h"
#include "jpr/status.h"
#include "jpr/tick.h"
#include "inline_hook_arch.h"

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool condition, const char* what, const char* file, int line) {
    gChecks++;
    if (condition)
        return;
    gFailures++;
    fprintf(stderr, "FAIL %s:%d  %s\n", file, line, what);
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

// ------------------------------------------------------------------- json

void testJson() {
    std::string error;
    auto value = jpr::json::parse(R"({
        // a comment
        "a": 1, "b": [1, 2], "c": "x\ny", "d": true, "e": null,
        /* another */ "f": -1.5e2,
    })",
                                  &error);
    CHECK(error.empty());
    CHECK(value.isObject());
    CHECK(value.find("a") && value.find("a")->asNumber() == 1);
    CHECK(value.find("b") && value.find("b")->array().size() == 2);
    CHECK(value.find("c") && value.find("c")->asString() == "x\ny");
    CHECK(value.find("d") && value.find("d")->asBool());
    CHECK(value.find("e") && value.find("e")->isNull());
    CHECK(value.find("f") && value.find("f")->asNumber() == -150.0);
    CHECK(value.find("zzz") == nullptr);

    // Round trip.
    auto again = jpr::json::parse(jpr::json::dump(value), &error);
    CHECK(error.empty());
    CHECK(again.find("c")->asString() == "x\ny");
    CHECK(again.find("f")->asNumber() == -150.0);

    jpr::json::parse("{\"a\": }", &error);
    CHECK(!error.empty());
    jpr::json::parse("{\"a\": 1", &error);
    CHECK(!error.empty());
}

// --------------------------------------------------------------- settings

void testSettings() {
    auto node = jpr::json::parse(R"({
        "enabled": true,
        "chance": 85,
        "chance_str": "42%",
        "hold_ms": [160, 90],
        "fixed_ms": 120,
        "key": "space",
        "letter": "w",
        "mode": "nonsense",
        "typo_here": 1
    })",
                                 nullptr);

    jpr::SettingSet set(&node, "modules.test");

    bool enabled = false;
    double chance = 0, chanceStr = 0;
    jpr::IntRange hold, fixed, missing;
    int key = 0, letter = 0;
    std::string mode;

    set.boolean("enabled", enabled, false);
    set.chance("chance", chance, 0.5);
    set.chance("chance_str", chanceStr, 0.5);
    set.range("hold_ms", hold, 1, 2);
    set.range("fixed_ms", fixed, 1, 2);
    set.range("absent_ms", missing, 7, 9);
    set.key("key", key, 0);
    set.key("letter", letter, 0);
    set.choice("mode", mode, "uniform", {"uniform", "normal"});

    CHECK(enabled);
    CHECK(chance == 0.85);           // bare 85 read as a percentage
    CHECK(chanceStr == 0.42);        // "42%" string form
    CHECK(hold.min == 90 && hold.max == 160);  // reversed bounds swapped
    CHECK(fixed.min == 120 && fixed.max == 120);  // bare number means fixed
    CHECK(missing.min == 7 && missing.max == 9);  // default when absent
    CHECK(key == jpr::keycode::Space);
    CHECK(letter == 'W');
    CHECK(mode == "uniform");  // invalid choice falls back

    CHECK(set.warnings().size() == 2);  // reversed range + bad choice
    auto unknown = set.unknownKeys();
    CHECK(unknown.size() == 1 && unknown[0] == "typo_here");
    CHECK(set.described().size() == 9);
}

// --------------------------------------------------------------- keycodes

void testKeycodes() {
    CHECK(jpr::parseKeyName("space", 0) == 32);
    CHECK(jpr::parseKeyName("SPACE", 0) == 32);
    CHECK(jpr::parseKeyName("page_up", 0) == 33);
    CHECK(jpr::parseKeyName("Page Up", 0) == 33);
    CHECK(jpr::parseKeyName("w", 0) == 'W');
    CHECK(jpr::parseKeyName("f5", 0) == 116);
    CHECK(jpr::parseKeyName("0x20", 0) == 32);
    CHECK(jpr::parseKeyName("32", 0) == 32);
    CHECK(jpr::parseKeyName("not-a-key", -7) == -7);
    CHECK(!strcmp(jpr::keyName(32), "space"));
    CHECK(!strcmp(jpr::keyName('W'), "w"));
}

// ----------------------------------------------------------------- random

void testRandom() {
    auto& random = jpr::Random::instance();
    random.reseed(12345);

    CHECK(random.chance(0.0) == false);
    CHECK(random.chance(1.0) == true);

    int hits = 0;
    for (int i = 0; i < 20000; i++) {
        if (random.chance(0.25))
            hits++;
    }
    CHECK(hits > 4400 && hits < 5600);  // ~5000 expected

    jpr::IntRange range(90, 160);
    bool sawMin = false, sawMax = false;
    for (int i = 0; i < 20000; i++) {
        int value = random.uniform(range);
        CHECK(value >= 90 && value <= 160);
        sawMin |= value == 90;
        sawMax |= value == 160;
    }
    CHECK(sawMin && sawMax);

    // The normal draw must stay inside the range too.
    for (int i = 0; i < 20000; i++) {
        int value = random.pick(range, jpr::Distribution::Normal);
        CHECK(value >= 90 && value <= 160);
    }

    // A degenerate range is a constant.
    CHECK(random.pick(jpr::IntRange(5, 5), jpr::Distribution::Normal) == 5);
    CHECK(random.uniform(7, 3) == 7);
}

// ---------------------------------------------------------------- sigscan

void testSigscan() {
    jpr::sig::Pattern pattern;
    std::string error;

    CHECK(jpr::sig::parsePattern("48 8B 05 ?? ?? ?? ?? 48 85 C0", pattern, &error));
    CHECK(error.empty());
    CHECK(pattern.size() == 10);
    CHECK(pattern.wildcard[3] && !pattern.wildcard[0]);

    // Alternative input spellings.
    jpr::sig::Pattern alternative;
    CHECK(jpr::sig::parsePattern("\\x48\\x8B\\x05\\x48", alternative, &error));
    CHECK(alternative.size() == 4);

    CHECK(!jpr::sig::parsePattern("?? 48 8B 05", pattern, &error));      // leading wildcard
    CHECK(!jpr::sig::parsePattern("48 8B", pattern, &error));            // too short
    CHECK(!jpr::sig::parsePattern("zz zz zz zz zz", pattern, &error));   // not hex
    CHECK(!jpr::sig::parsePattern("", pattern, &error));

    static const uint8_t haystack[] = {
        0x00, 0x11, 0x48, 0x8B, 0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0x48, 0x85, 0xC0, 0x00,
        0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44, 0x48, 0x85, 0xC0,
    };
    auto begin = (uintptr_t)haystack;
    auto end = begin + sizeof(haystack);

    CHECK(jpr::sig::parsePattern("48 8B 05 ?? ?? ?? ?? 48 85 C0", pattern, &error));
    CHECK(jpr::sig::scan(pattern, begin, end, 0) == (void*)(haystack + 2));
    CHECK(jpr::sig::scan(pattern, begin, end, 1) == (void*)(haystack + 13));
    CHECK(jpr::sig::scan(pattern, begin, end, 2) == nullptr);
    CHECK(jpr::sig::countMatches(pattern, begin, end) == 2);

    jpr::sig::Pattern absent;
    CHECK(jpr::sig::parsePattern("DE AD BE EF CA FE", absent, &error));
    CHECK(jpr::sig::scan(absent, begin, end) == nullptr);
    CHECK(jpr::sig::countMatches(absent, begin, end) == 0);
}

// ------------------------------------------------------------ x86 decoder

void testDecoder() {
#if defined(__x86_64__)
    struct Case {
        const char* what;
        std::vector<uint8_t> bytes;
        size_t length;
        bool relative;
        bool ripRelative;
    };

    const Case cases[] = {
        {"endbr64", {0xF3, 0x0F, 0x1E, 0xFA}, 4, false, false},
        {"push rbp", {0x55}, 1, false, false},
        {"mov rbp, rsp", {0x48, 0x89, 0xE5}, 3, false, false},
        {"sub rsp, 0x20", {0x48, 0x83, 0xEC, 0x20}, 4, false, false},
        {"sub rsp, 0x120", {0x48, 0x81, 0xEC, 0x20, 0x01, 0x00, 0x00}, 7, false, false},
        {"push r15", {0x41, 0x57}, 2, false, false},
        {"mov eax, 1", {0xB8, 0x01, 0x00, 0x00, 0x00}, 5, false, false},
        {"movabs rax, imm64", {0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8}, 10, false, false},
        {"mov rax, [rip+0x1234]", {0x48, 0x8B, 0x05, 0x34, 0x12, 0x00, 0x00}, 7, false, true},
        {"lea rdi, [rbp-0x10]", {0x48, 0x8D, 0x7D, 0xF0}, 4, false, false},
        {"mov [rsp+0x8], rbx", {0x48, 0x89, 0x5C, 0x24, 0x08}, 5, false, false},
        {"test eax, eax", {0x85, 0xC0}, 2, false, false},
        {"xorps xmm0, xmm0", {0x0F, 0x57, 0xC0}, 3, false, false},
        {"movss [rsp], xmm0", {0xF3, 0x0F, 0x11, 0x04, 0x24}, 5, false, false},
        {"call rel32", {0xE8, 0x00, 0x00, 0x00, 0x00}, 5, true, false},
        {"jmp rel8", {0xEB, 0x10}, 2, true, false},
        {"jne rel32", {0x0F, 0x85, 0x00, 0x00, 0x00, 0x00}, 6, true, false},
        {"ret", {0xC3}, 1, false, false},
        {"cmp byte [rax], 0", {0x80, 0x38, 0x00}, 3, false, false},
        {"mov dword [rax+0x10], 1", {0xC7, 0x40, 0x10, 0x01, 0x00, 0x00, 0x00}, 7, false, false},
        {"mov rax, [rax+rcx*8+0x10]", {0x48, 0x8B, 0x44, 0xC8, 0x10}, 5, false, false},
    };

    for (auto const& item : cases) {
        jpr::hook::arch::Instruction instruction;
        bool ok = jpr::hook::arch::decode(item.bytes.data(), instruction);
        if (!ok || instruction.length != item.length || instruction.relativeBranch != item.relative ||
            instruction.ripRelative != item.ripRelative) {
            fprintf(stderr, "FAIL decoder: %s -> ok=%d len=%zu (want %zu) rel=%d rip=%d\n", item.what, ok,
                    instruction.length, item.length, instruction.relativeBranch, instruction.ripRelative);
            gFailures++;
        }
        gChecks++;
    }

    // RIP relative relocation must re-base the displacement.
    uint8_t source[16] = {0x48, 0x8B, 0x05, 0x00, 0x10, 0x00, 0x00};
    uint8_t destination[16] = {};
    jpr::hook::arch::Instruction instruction;
    CHECK(jpr::hook::arch::decode(source, instruction));
    CHECK(jpr::hook::arch::relocate(source, destination, instruction));
    int32_t moved;
    memcpy(&moved, destination + 3, sizeof(moved));
    int64_t originalTarget = (int64_t)(source + 7) + 0x1000;
    int64_t movedTarget = (int64_t)(destination + 7) + moved;
    CHECK(originalTarget == movedTarget);
#endif
}

// ------------------------------------------------- auto jump reset module

std::vector<std::pair<int, bool>> gCaptured;
int64_t gClock = 0;

int64_t testClock() { return gClock; }

void capture(int key, bool down) { gCaptured.emplace_back(key, down); }

// Steps the clock in 1ms slices so the module sees every deadline it set.
void advance(int64_t milliseconds) {
    for (int64_t i = 0; i < milliseconds; i++) {
        gClock++;
        jpr::tick::pump(true);
    }
}

std::string writeConfig(const char* body) {
    std::string path = "/tmp/jpr-test-config.json";
    FILE* file = fopen(path.c_str(), "wb");
    fwrite(body, 1, strlen(body), file);
    fclose(file);
    return path;
}

void testAutoJumpReset() {
    jpr::setClockForTesting(testClock);
    jpr::keyboard::enableTestCapture(capture);

    auto& config = jpr::Config::instance();
    config.setPathForTesting(writeConfig(R"({
        "log_level": "error",
        "modules": {
            "auto_jump_reset": {
                "enabled": true,
                "chance": 1.0,
                "delay_ms": [10, 10],
                "hold_ms": [100, 100],
                "release_ms": [50, 50],
                "repeats": 2,
                "cooldown_ms": [500, 500],
                "distribution": "uniform",
                "key": "space"
            }
        }
    })"));
    config.load();
    auto& modules = jpr::ModuleManager::instance();
    modules.applyConfig();

    CHECK(modules.modules().size() == 1);
    CHECK(modules.anyWantsHurtEvents());

    // A hit: two presses of 100ms, 10ms apart at the start, 50ms between.
    gCaptured.clear();
    jpr::HurtEvent hit;
    hit.now = gClock;
    hit.damage = 3.0f;
    modules.dispatchLocalPlayerHurt(hit);

    advance(5);
    CHECK(gCaptured.empty());  // still inside delay_ms

    advance(10);
    CHECK(gCaptured.size() == 1);
    CHECK(gCaptured[0].first == jpr::keycode::Space && gCaptured[0].second == true);

    advance(100);
    CHECK(gCaptured.size() == 2);
    CHECK(gCaptured[1].second == false);  // released after hold_ms

    advance(60);
    CHECK(gCaptured.size() == 3);
    CHECK(gCaptured[2].second == true);  // second press after release_ms

    advance(105);
    CHECK(gCaptured.size() == 4);
    CHECK(gCaptured[3].second == false);

    // Cooldown: a hit during it is ignored.
    gCaptured.clear();
    hit.now = gClock;
    modules.dispatchLocalPlayerHurt(hit);
    advance(200);
    CHECK(gCaptured.empty());

    // Once the cooldown lapses it reacts again.
    advance(400);
    hit.now = gClock;
    modules.dispatchLocalPlayerHurt(hit);
    advance(20);
    CHECK(gCaptured.size() == 1 && gCaptured[0].second == true);
    // repeats is 2 here, so drain the whole sequence: press, hold, gap, press,
    // hold. A config reload mid-sequence deliberately lets the sequence finish
    // rather than leaving a key held.
    advance(400);
    CHECK(gCaptured.size() == 4 && gCaptured.back().second == false);

    // chance = 0 must never fire.
    config.setPathForTesting(writeConfig(R"({
        "log_level": "error",
        "modules": { "auto_jump_reset": {
            "enabled": true, "chance": 0.0, "delay_ms": 0, "hold_ms": 10,
            "cooldown_ms": 0, "distribution": "uniform"
        }}
    })"));
    config.load();
    modules.applyConfig();

    gCaptured.clear();
    for (int i = 0; i < 50; i++) {
        hit.now = gClock;
        modules.dispatchLocalPlayerHurt(hit);
        advance(5);
    }
    CHECK(gCaptured.empty());

    // A disabled module must not react, and must not be asked for hurt events.
    config.setPathForTesting(writeConfig(R"({
        "log_level": "error",
        "modules": { "auto_jump_reset": { "enabled": false } }
    })"));
    config.load();
    modules.applyConfig();
    CHECK(!modules.anyWantsHurtEvents());

    gCaptured.clear();
    hit.now = gClock;
    modules.dispatchLocalPlayerHurt(hit);
    advance(300);
    CHECK(gCaptured.empty());

    jpr::keyboard::enableTestCapture(nullptr);
    jpr::setClockForTesting(nullptr);
}

// Statistical check that `chance` actually means what it says.
void testChanceRate() {
    jpr::setClockForTesting(testClock);
    jpr::keyboard::enableTestCapture(capture);

    auto& config = jpr::Config::instance();
    config.setPathForTesting(writeConfig(R"({
        "log_level": "error",
        "modules": { "auto_jump_reset": {
            "enabled": true, "chance": 0.5, "delay_ms": 0, "hold_ms": 1,
            "release_ms": 0, "repeats": 1, "cooldown_ms": 0, "distribution": "uniform"
        }}
    })"));
    config.load();
    auto& modules = jpr::ModuleManager::instance();
    modules.applyConfig();

    gCaptured.clear();
    const int trials = 4000;
    for (int i = 0; i < trials; i++) {
        jpr::HurtEvent hit;
        hit.now = gClock;
        modules.dispatchLocalPlayerHurt(hit);
        advance(6);  // long enough for a whole press/release to complete
    }

    int presses = 0;
    for (auto const& item : gCaptured) {
        if (item.second)
            presses++;
    }
    CHECK(presses > trials * 0.45 && presses < trials * 0.55);
    if (presses <= trials * 0.45 || presses >= trials * 0.55)
        fprintf(stderr, "  chance 0.5 produced %d/%d presses\n", presses, trials);

    jpr::keyboard::enableTestCapture(nullptr);
    jpr::setClockForTesting(nullptr);
}

// debug_auto_fire_ms must drive the whole sequence without reading anything
// from the game, so it still works when the Keyboard symbols are missing.
void testAutoFire() {
    jpr::setClockForTesting(testClock);
    jpr::keyboard::enableTestCapture(capture);

    auto& config = jpr::Config::instance();
    config.setPathForTesting(writeConfig(R"({
        "log_level": "error",
        "modules": { "auto_jump_reset": {
            "enabled": true, "chance": 1.0, "delay_ms": 0, "hold_ms": 10,
            "release_ms": 0, "repeats": 1, "cooldown_ms": 0,
            "distribution": "uniform", "debug_auto_fire_ms": 100
        }}
    })"));
    config.load();
    auto& modules = jpr::ModuleManager::instance();
    modules.applyConfig();

    gCaptured.clear();
    advance(1000);

    int presses = 0;
    for (auto const& item : gCaptured) {
        if (item.second)
            presses++;
    }
    // One every 100ms over a second, give or take the boundary.
    CHECK(presses >= 9 && presses <= 11);
    if (presses < 9 || presses > 11)
        fprintf(stderr, "  auto fire produced %d presses over 1000ms\n", presses);

    // Zero must switch it off.
    config.setPathForTesting(writeConfig(R"({
        "log_level": "error",
        "modules": { "auto_jump_reset": {
            "enabled": true, "chance": 1.0, "hold_ms": 10, "cooldown_ms": 0,
            "debug_auto_fire_ms": 0
        }}
    })"));
    config.load();
    modules.applyConfig();

    advance(50);
    gCaptured.clear();
    advance(1000);
    CHECK(gCaptured.empty());

    jpr::keyboard::enableTestCapture(nullptr);
    jpr::setClockForTesting(nullptr);
}

// The status file is the mod's only self-report when nobody reads the game
// log, so it has to appear and to name the thing that is wrong.
void testStatusFile() {
    jpr::keyboard::enableTestCapture(nullptr);  // injection unavailable

    auto& config = jpr::Config::instance();
    std::string dir = "/tmp/jpr-status-test/nested/";
    (void)!system("rm -rf /tmp/jpr-status-test");
    config.setPathForTesting(dir + "jpr.json");
    config.load();
    jpr::ModuleManager::instance().applyConfig();

    jpr::status::markLoaded("mod_init");

    std::string body;
    FILE* file = fopen((dir + "status.txt").c_str(), "rb");
    CHECK(file != nullptr);  // written even though the directory did not exist
    if (!file)
        return;
    char buffer[4096];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
        body.append(buffer, read);
    fclose(file);

    CHECK(body.find("jpr status") != std::string::npos);
    CHECK(body.find("mod_init") != std::string::npos);
    CHECK(body.find("KEY INJECTION") != std::string::npos);
    CHECK(body.find("UNAVAILABLE") != std::string::npos);
    CHECK(body.find("auto_jump_reset") != std::string::npos);
    CHECK(body.find("MODULES") != std::string::npos);

    // With injection working the verdict has to flip.
    jpr::keyboard::enableTestCapture(capture);
    jpr::status::write();
    body.clear();
    file = fopen((dir + "status.txt").c_str(), "rb");
    CHECK(file != nullptr);
    if (file) {
        while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
            body.append(buffer, read);
        fclose(file);
    }
    CHECK(body.find("status             READY") != std::string::npos);

    jpr::keyboard::enableTestCapture(nullptr);
    (void)!system("rm -rf /tmp/jpr-status-test");
}

}  // namespace

int main() {
    jpr::setLogLevel(jpr::LogLevel::Error);

    testJson();
    testSettings();
    testKeycodes();
    testRandom();
    testSigscan();
    testDecoder();
    testAutoJumpReset();
    testChanceRate();
    testAutoFire();
    testStatusFile();

    printf("%d checks, %d failure(s)\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
