#include "data/RGRandom.hpp"

#include <cstdint>

namespace Game {

namespace {

// Unity 2017.4 (UnityEngine.Random) InitState fill constant.
// InitState builds the 4-word Xorshift128 state from the 32-bit seed using the
// classic Mersenne-Twister-style LCG fill that Unity uses internally:
//   s0 = seed; s_{i+1} = s_i * 0x6C078965 + 1.
// FAITHFUL: see manual_flags - this is the best-documented Unity 2017.4
// InitState; bit-exactness must be validated against captured reference output.
constexpr std::uint32_t kInitMul = 0x6C078965u;

// Unity 2017.4 float draw (validated against the reverse-engineered reference,
// macklinb gist): value = (uint32)(word << 9) / 0xFFFFFFFF. The word is left-
// shifted by 9 (keeping the low 23 bits, dropping the high 9) and divided by the
// full 32-bit max, yielding a float in [0,1). NOT a (word & 0xFFFFFF)/2^24 draw.
constexpr float kUInt32Max = 4294967295.0F; // 0xFFFFFFFF

} // namespace

// FAITHFUL: RGRandom .ctor (TypeDefIndex 5167) - default (unseeded) stream.
RGRandom::RGRandom() = default;

// FAITHFUL: Unity 2017.4 UnityEngine.Random.InitState seeding.
void RGRandom::InitState(int seed) {
    std::uint32_t s = static_cast<std::uint32_t>(seed);
    m_S0 = s;
    s = s * kInitMul + 1u;
    m_S1 = s;
    s = s * kInitMul + 1u;
    m_S2 = s;
    s = s * kInitMul + 1u;
    m_S3 = s;
}

// FAITHFUL: Xorshift128 (Marsaglia) state advance - Unity's core generator.
// Standard 4-word xorshift128: t = s0 ^ (s0 << 11); rotate s0<-s1<-s2<-s3;
// s3 = (s3 ^ (s3 >> 19)) ^ (t ^ (t >> 8)).
std::uint32_t RGRandom::NextState() {
    std::uint32_t t = m_S0;
    t ^= t << 11;
    t ^= t >> 8;
    m_S0 = m_S1;
    m_S1 = m_S2;
    m_S2 = m_S3;
    m_S3 = (m_S3 ^ (m_S3 >> 19)) ^ t;
    return m_S3;
}

float RGRandom::NextFloat01() {
    // Unity: (uint32)(XORShift() << 9) / 0xFFFFFFFF -> [0,1).
    const std::uint32_t word = static_cast<std::uint32_t>(NextState() << 9);
    return static_cast<float>(word) / kUInt32Max;
}

void RGRandom::EnsureSeeded() {
    // decomp: lazy-seed from the game-info seed if not yet readied.
    if (m_SeedState != kGetSeed) {
        SetRandomSeed(FallbackSeed);
    }
}

// FAITHFUL: RGRandom__SetRandomSeed @ 0x4EC620.
// Original: save global Random.state, InitState(seed), capture seeded state into
// this_state, restore global state, set seed_state = GET_SEED (== 3). Because
// our state is fully inline, the save/restore of an external global is a no-op,
// so we seed directly into this stream's state.
void RGRandom::SetRandomSeed(int seed) {
    InitState(seed);
    m_SeedState = kGetSeed;
}

// FAITHFUL: RGRandom__Range(int) @ 0x4FACFC. Unity int semantics: max EXCLUSIVE.
int RGRandom::Range(int minInclusive, int maxExclusive) {
    EnsureSeeded();

    // Degenerate range -> Unity returns the lower bound.
    if (maxExclusive <= minInclusive) {
        return minInclusive;
    }

    // Unity reduces the next 32-bit word across the half-open span. Use unsigned
    // span math so a wide [INT_MIN, INT_MAX) range cannot overflow, then add the
    // reduced offset back onto min.
    std::uint32_t span =
        static_cast<std::uint32_t>(maxExclusive) - static_cast<std::uint32_t>(minInclusive);
    std::uint32_t r = NextState() % span;
    return static_cast<int>(static_cast<std::uint32_t>(minInclusive) + r);
}

// FAITHFUL: RGRandom__Range(float) @ 0x4FAE1C. Unity float semantics: max INCLUSIVE.
float RGRandom::Range(float minInclusive, float maxInclusive) {
    EnsureSeeded();

    // Unity Range(float): (min - max) * value + max, with value in [0,1).
    // value == 0 yields exactly max (max-inclusive); min is approached as
    // value -> 1. This exact form (not min + value*(max-min)) matches the
    // reverse-engineered reference and keeps the draw bit-faithful.
    const float value = NextFloat01();
    return (minInclusive - maxInclusive) * value + maxInclusive;
}

} // namespace Game
