#include "combat/GunMagicBow.hpp"

namespace Game {

// FAITHFUL: GunMagicBow__Update @ game_full.c:967260-967261. The decomp's gate:
//   iVar1 = Animator.GetBool(firing);
//   if (iVar1 == 1)
//     if (*(float *)(param_1 + 0x8c) < *(float *)(param_1 + 0x90)) { ...tick... }
// The charge accumulator is advanced ONLY while firing && charge < max_charge.
// The increment amount itself is in the truncated Singleton<RGGameProcess> tail
// (967262-967268, "Subroutine does not return") -- OWNER. We model the
// predicate; the Animator.GetBool firing read is also OWNER. NO RGRandom draw.
bool GunMagicBow::ShouldTickCharge(bool firing, float charge, float maxCharge) {
    return firing && charge < maxCharge;
}

// FAITHFUL: GunMagicBow__Attack @ game_full.c:967314 --
//   fVar4 = *(float *)(param_3 + 0x8c) / *(float *)(param_3 + 0x90).
// charge (owner+0x8c) over max_charge (owner+0x90). No clamp in the decomp: an
// over-charge yields a ratio > 1 exactly as the division produces. A
// non-positive divisor is impossible in the original (Update only ticks toward
// the cap while charge < max_charge), so we guard the degenerate case by
// returning 0 rather than dividing by zero -- we NEVER invent a value the decomp
// does not compute. NO RGRandom draw.
float GunMagicBow::ChargeRatio(float charge, float maxCharge) {
    if (maxCharge <= 0.0F) {
        return 0.0F;
    }
    return charge / maxCharge;
}

// FAITHFUL: GunMagicBow__Attack @ game_full.c:967315-967325. The base-velocity
// components are read via VectorSignedToFloat:
//   fVar1 = base x (owner+0x80), fVar2 = base y (owner+0x84),
//   fVar3 = base z (owner+0x88),
// each scaled by the charge ratio fVar4 and handed to the owner's
// GetComponent<RGBullet> velocity apply:
//   x -> fVar4 * fVar1, y -> fVar4 * fVar2 (both @ 967325),
//   z -> (int)(fVar4 * fVar3) (967318; the int truncation is the component-store
//   concern, OWNER -- the float product is the recoverable scalar).
// Every axis is the same form: velocity = chargeRatio * base. The
// Instantiate/GetComponent<RGBullet> apply (967324) is OWNER. NO RGRandom draw.
float GunMagicBow::ScaledVelocity(float baseComponent, float chargeRatio) {
    return chargeRatio * baseComponent;
}

} // namespace Game
