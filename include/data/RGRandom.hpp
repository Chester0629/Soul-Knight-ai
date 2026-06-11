#ifndef RG_RANDOM_HPP
#define RG_RANDOM_HPP

#include <cstdint>

namespace Game {

/**
 * @class RGRandom
 * @brief Per-instance deterministic RNG stream - the root of Soul Knight's
 *        networked dungeon/loot/enemy reproducibility.
 *
 * Faithful port of the original game's @c RGRandom (TypeDefIndex 5167). In the
 * binary, @c RGRandom does not call @c UnityEngine.Random directly: it keeps a
 * private @c Random.State snapshot (4x uint32) and, on every draw, swaps that
 * state into the global Unity RNG, draws, and saves the advanced state back.
 * That "borrow -> restore" dance makes every @c RGRandom instance an isolated,
 * mutually independent deterministic sequence: the map uses one stream, each
 * room its own, each enemy placer its own, and call order never cross-pollutes
 * them.
 *
 * Because each instance owns its full state, we don't need a real global swap
 * here - we simply carry the 4x uint32 state inline and advance it directly.
 * This is behaviourally identical to the original swap pattern (the global RNG
 * is only ever a scratch register in the original; nothing observes it between
 * a load and the matching save).
 *
 * The PRNG itself reproduces Unity 2017.4's @c UnityEngine.Random:
 *   - state layout: 4x uint32 (s0,s1,s2,s3), matching Random.State at +0xC;
 *   - seeding: Unity's @c InitState(seed) algorithm;
 *   - core generator: Xorshift128 (Marsaglia), advancing the 4-word state;
 *   - @c Range(int,int): max EXCLUSIVE (Unity int semantics);
 *   - @c Range(float,float): max INCLUSIVE (Unity float semantics).
 *
 * VALIDATION STATUS: the integer path (InitState, Xorshift128 advance, Range
 * int) is VERIFIED bit-exact against the reverse-engineered Unity reference
 * (macklinb gist): InitState(1234) -> first XORShift word 3463400838 ->
 * Range(0, INT_MAX) 1315917191, all reproduced exactly (see RGRandomTest
 * UnityParity_*). InitState constant 1812433253 (0x6C078965) and the 11/8/19
 * shift triple are confirmed. The float path uses Unity's
 * (uint32)(word << 9) / 0xFFFFFFFF draw and (min-max)*value+max mapping.
 * Residual: only the no-seed default and float edge inclusivity remain
 * unconfirmed against the shipped game; the all-int generation/loot/AI streams
 * are Unity-faithful.
 */
class RGRandom {
public:
    /**
     * @brief Construct an unseeded stream.
     *
     * Mirrors the original's @c seed_state == C_NOT_READY: Seeded() is false and
     * the first draw lazily seeds from FallbackSeed (see EnsureSeeded()).
     */
    RGRandom();

    /**
     * @brief Seed this stream's private state via Unity's InitState(seed).
     * @param seed The deterministic seed (network-authoritative in the original).
     *
     * FAITHFUL to RGRandom__SetRandomSeed @ 0x4EC620: snapshots the (here inline)
     * RNG state, runs InitState, captures the seeded state, and marks the stream
     * ready (seed_state = GET_SEED == 3).
     */
    void SetRandomSeed(int seed);

    /**
     * @brief Draw an int in [minInclusive, maxExclusive).
     * @return @c minInclusive when the range is empty (max <= min), matching
     *         Unity's degenerate-range behaviour.
     *
     * FAITHFUL to RGRandom__Range(int) @ 0x4FACFC: lazy-seeds, loads state,
     * draws via Unity Random.Range(int) (max exclusive), saves advanced state.
     */
    int Range(int minInclusive, int maxExclusive);

    /**
     * @brief Draw a float in [minInclusive, maxInclusive].
     *
     * FAITHFUL to RGRandom__Range(float) @ 0x4FAE1C: lazy-seeds, loads state,
     * draws via Unity Random.Range(float) (max inclusive), saves advanced state.
     */
    float Range(float minInclusive, float maxInclusive);

    /// @return true once SetRandomSeed has been called (seed_state == GET_SEED).
    bool Seeded() const { return m_SeedState == kGetSeed; }

    /// Seed used by the lazy first-draw seeding when the stream is still unseeded.
    /// In the original this came from RGGameInfo.map_random_seed; we expose it as
    /// a settable field so callers can wire the authoritative seed in.
    int FallbackSeed = 0;

private:
    /// Mirrors the original RANDOM_STATE enum value for "seeded".
    /// (C_NOT_READY=0, C_READY=1, S_SEND_SEED=2, GET_SEED=3.)
    static constexpr int kNotReady = 0;
    static constexpr int kGetSeed = 3;

    /// Run Unity's InitState(seed) seeding into (s0..s3).
    void InitState(int seed);
    /// Advance the Xorshift128 state and return the new 32-bit word.
    std::uint32_t NextState();
    /// Next float in [0,1) from a fresh state advance (24-bit mantissa style).
    float NextFloat01();
    /// If not yet seeded, lazily seed from FallbackSeed (mirrors EnsureSeeded()).
    void EnsureSeeded();

    // Private Random.State snapshot (Random.State @ +0xC in the original):
    //   s0:int @0x0  s1:int @0x4  s2:int @0x8  s3:int @0xC.
    std::uint32_t m_S0 = 0;
    std::uint32_t m_S1 = 0;
    std::uint32_t m_S2 = 0;
    std::uint32_t m_S3 = 0;

    int m_SeedState = kNotReady; ///< seed_state @ +0x8 in the original.
};

} // namespace Game

#endif /* RG_RANDOM_HPP */
