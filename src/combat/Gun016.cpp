#include "combat/Gun016.hpp"

#include <algorithm>

namespace Game {

// FAITHFUL: Gun016__Attack @ game_full.c:964610 -- fVar8 / fVar7, where
// fVar8 = shoot_time (owner+0x70) and fVar7 = shoot_max_time (owner+0x6c).
// No clamp in the decomp: an over-heat yields a ratio > 1 exactly as the
// division produces. shoot_max_time is 2.0f from the ctor and Update only ticks
// toward it, so a non-positive divisor is impossible in the original; we guard
// the degenerate case by returning 0 rather than dividing by zero (we NEVER
// invent a value the decomp does not compute).
float Gun016::HeatRatio(float shootTime, float shootMaxTime) {
    if (shootMaxTime <= 0.0F) {
        return 0.0F;
    }
    return shootTime / shootMaxTime;
}

// FAITHFUL: Gun016__Update @ game_full.c:964580. The decomp's gate is
//   GetBool(firing) == 1 && shoot_time(0x70) < shoot_max_time(0x6c)
// before the (truncated, OWNER) heat-timer increment. We model the predicate;
// the increment amount lives in the unreturned Singleton<RGGameProcess> tail.
bool Gun016::ShouldTickHeat(bool firing, float shootTime, float shootMaxTime) {
    return firing && shootTime < shootMaxTime;
}

// FAITHFUL: Gun016__Attack @ game_full.c:964610 --
//   uVar2 = Mathf.Max(0, fVar5 + fVar5*(iVar1+0x20) + (fVar8/fVar7)*fVar6)
// fVar5 = base recoil angle (owner+0x30); (iVar1+0x20) = the per-bullet recoil
// multiplier the owner supplies via its bullet/component fetch; (fVar8/fVar7) =
// heat ratio; fVar6 = max_deviation (owner+0x80 = -15, hence the heat term
// tightens the cone). Mathf.Max(0, .) floors the spread at a straight shot.
// Purely deterministic; NO RGRandom draw on this path.
float Gun016::Spread(float baseAngle, float recoilMul, float heatRatio) {
    const float raw =
        baseAngle + baseAngle * recoilMul + heatRatio * kMaxDeviation;
    return std::max(0.0F, raw);
}

// FAITHFUL: Gun016__Attack @ game_full.c:964610 --
//   RGRandom__Range(owner+0x60, spread ^ 0x80000000, spread)
// The ^0x80000000 flips the float sign bit (== -spread); the draw is the
// canonical symmetric single-shot scatter Range(-spread, +spread). Draws
// EXACTLY ONE max-inclusive float from this weapon's stream. The muzzle spawn
// (Transform.get_position at the truncated tail) that consumes the angle is
// OWNER.
float Gun016::ScatterAngle(float spread) {
    return m_Rng.Range(-spread, spread);
}

} // namespace Game
