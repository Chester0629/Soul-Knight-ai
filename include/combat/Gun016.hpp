#ifndef GAME_GUN016_HPP
#define GAME_GUN016_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun016
 * @brief Faithful heat/spin-up minigun spread + scatter math for the "Gun016"
 *        weapon (Soul Knight 1.7.10).
 *
 * Per-content port. Gun016 is a heat ("spin-up") minigun: while the firing
 * animation bool is held, a heat timer (shoot_time, owner+0x70) ramps up toward
 * a cap (shoot_max_time, owner+0x6c = 2.0s). Each shot's spread is a base recoil
 * fan PLUS a heat-ratio-scaled term that uses a NEGATIVE max_deviation
 * (owner+0x80 = -15), so as heat builds the cone actually TIGHTENS (the term is
 * negative) until the Max(0, .) floor clamps it to a straight shot. The bullet's
 * scatter angle is then a single symmetric RGRandom draw over +/- that spread.
 *
 * This brain models ONLY the recoverable scalar/state math that feeds the
 * weapon's side-effects. The Animator.GetBool firing read, the heat-timer
 * increment itself (the truncated Singleton<RGGameProcess> tail in Update), the
 * bullet RGBullet/component fetch (the virtual call at vtable+0xf4 that supplies
 * the recoil multiplier), and the Transform.get_position muzzle spawn are all
 * OWNER concerns.
 *
 * Recoverable here:
 *   - the three ctor-configured fields (Gun016.ctor @ game_full.c:964550):
 *     shoot_max_time (owner+0x6c = 2.0f), max_anim_speed (owner+0x78 = 1.0f),
 *     max_deviation (owner+0x80 = -15, stored as int 0xfffffff1, read back
 *     through VectorSignedToFloat as a float);
 *   - the Update heat-tick GATE (Update @ game_full.c:964580): the heat timer is
 *     ticked up ONLY while firing && shoot_time < shoot_max_time. The increment
 *     amount lives in the truncated tail (OWNER), so we model the predicate, not
 *     the step;
 *   - the Attack spread formula (Attack @ game_full.c:964610):
 *     spread = Max(0, base + base*recoilMul + (shoot_time/shoot_max_time)*maxDev)
 *     where `base` is the recoil base angle (owner+0x30), `recoilMul` is the
 *     per-bullet recoil multiplier (component+0x20 via the owner's virtual
 *     fetch), and `maxDev` is the configured max_deviation (owner+0x80 = -15);
 *   - the symmetric scatter draw (Attack @ game_full.c:964610): ONE max-inclusive
 *     RGRandom float draw Range(-spread, +spread) on the weapon's stream
 *     (owner+0x60).
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun016 @ game_full.c:964550-964700.
 */
class Gun016 {
public:
    /// shoot_max_time: the heat cap (seconds) the heat timer ramps toward, and
    /// the divisor of the heat ratio. ctor writes the float immediate 0x40000000
    /// == 2.0f. FAITHFUL: Gun016.ctor @ game_full.c:964557 (*(owner+0x6c)).
    static constexpr float kShootMaxTime = 2.0F;

    /// max_anim_speed: the firing-animation speed cap. ctor writes 0x3f800000 ==
    /// 1.0f. Drives the Animator speed (OWNER); recorded for completeness, it
    /// does not enter the spread math. FAITHFUL: Gun016.ctor @
    /// game_full.c:964558 (*(owner+0x78)).
    static constexpr float kMaxAnimSpeed = 1.0F;

    /// max_deviation: the heat-ratio-scaled spread term's coefficient. ctor
    /// writes the int 0xfffffff1 == -15, read back through VectorSignedToFloat as
    /// -15.0f. NEGATIVE: building heat subtracts from the cone, tightening it.
    /// FAITHFUL: Gun016.ctor @ game_full.c:964559 (*(owner+0x80) = 0xfffffff1).
    static constexpr float kMaxDeviation = -15.0F;

    Gun016() = default;

    /// Seed the weapon's deterministic stream (owner+0x60). Exactly ONE float is
    /// drawn per ScatterAngle call; keep it in lockstep with the original.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Heat ratio = shoot_time / shoot_max_time.
     *
     * FAITHFUL: Gun016__Attack @ game_full.c:964610 (fVar8 / fVar7, where
     * fVar8 = owner+0x70 = shoot_time and fVar7 = owner+0x6c = shoot_max_time).
     * The decomp applies NO clamp; an over-heat (shoot_time > shoot_max_time)
     * yields a ratio > 1 exactly as the division produces. `shootMaxTime` is the
     * heat cap; a non-positive cap is impossible in the original (the ctor sets
     * 2.0f and Update only ticks toward it), so we guard against a divide-by-zero
     * by returning 0 in that degenerate case rather than inventing a value.
     */
    static float HeatRatio(float shootTime, float shootMaxTime);

    /**
     * @brief Whether the Update heat-tick fires this frame.
     *
     * FAITHFUL: Gun016__Update @ game_full.c:964580. The heat timer (owner+0x70)
     * is ticked up only when the firing-animation bool is held AND
     * shoot_time < shoot_max_time (owner+0x6c). The increment amount itself is in
     * the truncated Singleton<RGGameProcess> tail (OWNER), so we model only this
     * gate predicate, not the step.
     * @return true if the owner should advance shoot_time this frame.
     */
    static bool ShouldTickHeat(bool firing, float shootTime, float shootMaxTime);

    /**
     * @brief The shot spread (half-cone, degrees) for one bullet at a given heat.
     *
     * FAITHFUL: Gun016__Attack @ game_full.c:964610:
     *   uVar2 = Mathf.Max(0, fVar5 + fVar5*(iVar1+0x20) + (fVar8/fVar7)*fVar6)
     * with fVar5 = base recoil angle (owner+0x30), (iVar1+0x20) = the per-bullet
     * recoil multiplier supplied by the owner's bullet/component fetch, and
     * fVar6 = max_deviation (owner+0x80 = -15). The Max(0, .) floors the cone at
     * a straight shot. Purely deterministic; NO RGRandom draw here.
     * @param baseAngle  recoil base angle (owner+0x30, decomp fVar5).
     * @param recoilMul  per-bullet recoil multiplier (component+0x20).
     * @param heatRatio  shoot_time/shoot_max_time (see HeatRatio).
     * @return the non-negative half-cone spread fed to the scatter draw.
     */
    static float Spread(float baseAngle, float recoilMul, float heatRatio);

    /**
     * @brief One bullet's scatter angle: Range(-spread, +spread).
     *
     * FAITHFUL: Gun016__Attack @ game_full.c:964610
     * (RGRandom__Range(owner+0x60, spread ^ 0x80000000, spread) -- the
     * ^0x80000000 flips the float sign bit, i.e. -spread). Draws EXACTLY ONE
     * float from this weapon's stream (max-INCLUSIVE float Range), matching the
     * decomp's single draw per fired shot. `spread` is the Spread() result; the
     * Transform.get_position muzzle spawn that consumes this angle is OWNER.
     * @return the scatter angle in [-spread, +spread].
     */
    float ScatterAngle(float spread);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
};

} // namespace Game

#endif /* GAME_GUN016_HPP */
