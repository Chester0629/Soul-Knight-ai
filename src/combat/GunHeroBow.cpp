#include "combat/GunHeroBow.hpp"

namespace Game {

// FAITHFUL: GunHeroBow__Attack @ game_full.c:967052 --
//   *(float *)(param_1 + 0x98) / *(float *)(param_1 + 0x9c).
// charge (owner+0x98) over max_charge (owner+0x9c). No clamp in the decomp: an
// over-charge yields a ratio > 1 exactly as the division produces. A
// non-positive divisor is impossible in the original (the charge only ticks
// toward the cap), so we guard the degenerate case by returning 0 rather than
// dividing by zero -- we NEVER invent a value the decomp does not compute.
// NO RGRandom draw.
float GunHeroBow::ChargeRatio(float charge, float maxCharge) {
    if (maxCharge <= 0.0F) {
        return 0.0F;
    }
    return charge / maxCharge;
}

// FAITHFUL: GunHeroBow__Attack @ game_full.c:967052. The base speed component is
// read via VectorSignedToFloat (fVar2 = base owner+0x94 @ 967049) then scaled by
// the charge ratio: (int)((charge/max_charge) * fVar2). The int truncation is the
// owner component-store concern; the float product is the recoverable scalar. The
// other two base-velocity components (owner+0x90 @ 967050, owner+0x8c @ 967051)
// are read but passed through UNCHANGED, so only the 0x94 speed scales. The
// GetComponent<RGBullet> apply (967067-967076) is OWNER. NO RGRandom draw.
float GunHeroBow::ScaledSpeed(float baseSpeed, float chargeRatio) {
    return chargeRatio * baseSpeed;
}

// FAITHFUL: GunHeroBow__Attack @ game_full.c:967054 --
//   if (*(int *)(param_1 + 0xac) < 1) { RGMusicManager.PlayEffect(dry); return; }
// The bow fires only when arrow_count (owner+0xac) >= 1; an empty quiver plays the
// dry-release SFX (owner) and fires nothing. NO RGRandom draw.
bool GunHeroBow::HasArrows(int arrowCount) {
    return arrowCount >= kMinArrowsToFire;
}

// FAITHFUL: FUN_00b138f0 @ game_full.c:967091 (Attack's per-arrow loop
// continuation) -- if (*(int *)(unaff_r4 + 0xac) <= unaff_r7 + 1) stop, where
// unaff_r7 is the index just fired. So after firing index `firedIndex`, another
// fires only while firedIndex + 1 < arrow_count; the bow fires exactly
// arrow_count arrows (indices 0 .. arrow_count-1). The arrow spawn is OWNER.
// NO RGRandom draw.
bool GunHeroBow::ShouldFireNext(int firedIndex, int arrowCount) {
    return firedIndex + 1 < arrowCount;
}

} // namespace Game
