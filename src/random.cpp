#include "jpr/random.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <random>

namespace jpr {

namespace {
std::mutex gMutex;
}  // namespace

Distribution parseDistribution(const char* text, Distribution fallback) {
    if (!text)
        return fallback;
    if (!strcasecmp(text, "uniform"))
        return Distribution::Uniform;
    if (!strcasecmp(text, "normal") || !strcasecmp(text, "gaussian"))
        return Distribution::Normal;
    return fallback;
}

const char* distributionName(Distribution d) {
    return d == Distribution::Normal ? "normal" : "uniform";
}

Random& Random::instance() {
    static Random random;
    return random;
}

Random::Random() {
    std::random_device device;
    uint64_t seed = ((uint64_t)device() << 32) ^ device();
    seed ^= (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
    reseed(seed);
}

void Random::reseed(uint64_t seed) {
    std::lock_guard<std::mutex> lock(gMutex);
    // SplitMix64 expansion, so a single word seed still fills the state well.
    for (uint64_t& word : state_) {
        seed += 0x9E3779B97F4A7C15ull;
        uint64_t z = seed;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        word = z ^ (z >> 31);
    }
    if (!state_[0] && !state_[1])
        state_[0] = 0x853C49E6748FEA9Bull;
}

// xoroshiro128+
uint64_t Random::next() {
    uint64_t s0 = state_[0];
    uint64_t s1 = state_[1];
    uint64_t result = s0 + s1;
    s1 ^= s0;
    state_[0] = ((s0 << 55) | (s0 >> 9)) ^ s1 ^ (s1 << 14);
    state_[1] = (s1 << 36) | (s1 >> 28);
    return result;
}

bool Random::chance(double p) {
    if (p <= 0.0)
        return false;
    if (p >= 1.0)
        return true;
    std::lock_guard<std::mutex> lock(gMutex);
    // 53 bits of mantissa is plenty and avoids modulo bias entirely.
    double roll = (double)(next() >> 11) * (1.0 / 9007199254740992.0);
    return roll < p;
}

int Random::uniform(int min, int max) {
    if (max <= min)
        return min;
    uint64_t span = (uint64_t)(max - min) + 1;
    std::lock_guard<std::mutex> lock(gMutex);
    // Rejection sampling keeps the distribution flat across the whole span.
    uint64_t limit = UINT64_MAX - (UINT64_MAX % span);
    uint64_t draw;
    do {
        draw = next();
    } while (draw >= limit);
    return min + (int)(draw % span);
}

double Random::uniformReal(double min, double max) {
    if (max <= min)
        return min;
    std::lock_guard<std::mutex> lock(gMutex);
    double unit = (double)(next() >> 11) * (1.0 / 9007199254740992.0);
    return min + unit * (max - min);
}

int Random::pick(const IntRange& range, Distribution d) {
    if (range.max <= range.min)
        return range.min;
    if (d == Distribution::Uniform)
        return uniform(range.min, range.max);

    // Centre the bell on the midpoint and put the range edges at ~2.5 sigma,
    // then clamp, so the tails land inside the configured bounds.
    double mid = (range.min + range.max) / 2.0;
    double sigma = (range.max - range.min) / 5.0;
    double u1, u2;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        u1 = (double)(next() >> 11) * (1.0 / 9007199254740992.0);
        u2 = (double)(next() >> 11) * (1.0 / 9007199254740992.0);
    }
    if (u1 < 1e-12)
        u1 = 1e-12;
    double gauss = std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    long value = std::lround(mid + gauss * sigma);
    return range.clamp((int)value);
}

}  // namespace jpr
