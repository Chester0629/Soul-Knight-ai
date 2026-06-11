#include "combat/GunWaken.hpp"

namespace Game {

// FAITHFUL: GunWaken__Attack @ game_full.c:970451+970453 (inside the wakenFlag
// == 0 gate at 970452):
//   fVar4 = (float)VectorSignedToFloat(*(owner+0x30));   // base angle widened
//   fVar4 = fVar4 + fVar4 * *(float *)(component + 0x20); // + angle*recoil
// spread = angle + angle*recoil = angle*(1 + recoil). `angle` is the owner's
// base scatter field (owner+0x30, an int the decomp widens via the ARM vcvt
// surfaced as VectorSignedToFloat); `recoil` is a bullet-component float
// (component+0x20) supplied by the owner's vtable+0xf4 fetch. This is the
// canonical single-shot scatter shape (cf. Gun019::SpreadHalfAngle). NO RGRandom
// draw here.
float GunWaken::SpreadHalfAngle(float angle, float recoil) {
    return angle + angle * recoil;
}

// FAITHFUL: GunWaken__Attack @ game_full.c:970458 --
//   RGRandom__Range(*(owner+0x60), -fVar4, fVar4, 0): ONE max-INCLUSIVE float
// draw, the symmetric per-shot scatter. This line lives INSIDE the wakenFlag ==
// 0 gate (970452): it runs ONLY in normal mode. In awakened mode (flag != 0) the
// decomp skips this draw entirely and the stream is NOT advanced -- callers must
// honour that (see ResolveScatter). The muzzle Transform.get_position bullet
// spawn (owner+0x4c at 970460-970462) that follows is the OWNER's concern (the
// decomp's truncated tail).
float GunWaken::ScatterAngle(float spread) {
    return m_Rng.Range(-spread, spread);
}

// FAITHFUL: GunWaken__Attack @ game_full.c:970452-970459 -- the conditional-RNG
// decision (module note G5). The gate `if (*(int *)(owner+0x84) == 0)` selects:
//   - awakened (flag != 0): the whole spread+draw block is gated OUT. We return
//     0 and take NO RGRandom draw -- the stream stays exactly where it was, in
//     lockstep with the original (the gated-out path advances nothing).
//   - normal (flag == 0): compute spread (970451+970453) then take ONE draw
//     (970458).
float GunWaken::ResolveScatter(int wakenFlag, float angle, float recoil) {
    if (IsAwakened(wakenFlag)) {
        // Awakened: gated-out path -- perfect accuracy, ZERO draws (no advance).
        return 0.0F;
    }
    const float spread = SpreadHalfAngle(angle, recoil);
    return ScatterAngle(spread);
}

} // namespace Game
