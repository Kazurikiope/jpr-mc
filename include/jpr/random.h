#pragma once

#include <cstdint>

namespace jpr {

// An inclusive [min, max] integer range, as written in the config as [min, max].
struct IntRange {
    int min = 0;
    int max = 0;

    IntRange() = default;
    IntRange(int lo, int hi) : min(lo), max(hi) {}

    bool fixed() const { return min >= max; }
    int clamp(int v) const { return v < min ? min : (v > max ? max : v); }
};

// How a value is drawn from a range.
enum class Distribution {
    // Every value in the range is equally likely.
    Uniform,
    // Bell shaped around the midpoint, clamped to the range. Produces timings
    // that cluster the way a person's do rather than spreading out flatly.
    Normal,
};

Distribution parseDistribution(const char* text, Distribution fallback);
const char* distributionName(Distribution d);

// Thread safe source of randomness for the mod. Seeded from the system entropy
// source at first use, so two launches never share a timing sequence.
class Random {
public:
    static Random& instance();

    // Returns true with probability `p`, where p is 0.0 .. 1.0.
    bool chance(double p);

    // Uniform integer in [min, max]. Returns min when the range is degenerate.
    int uniform(int min, int max);

    int uniform(const IntRange& range) { return uniform(range.min, range.max); }

    // Draws from `range` using `d`.
    int pick(const IntRange& range, Distribution d);

    double uniformReal(double min, double max);

    void reseed(uint64_t seed);

private:
    Random();
    uint64_t next();

    uint64_t state_[2];
};

}  // namespace jpr
