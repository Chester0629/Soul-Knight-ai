#include "combat/Gun013.hpp"

namespace Game {

// FAITHFUL: Gun013__Attack @ game_full.c:964334 --
//   fVar4 = (float)VectorSignedToFloat(*(this+0x30), ...);   // baseAngle
//   fVar4 = fVar4 + fVar4 * *(float *)(iVar1 + 0x20);        // + baseAngle*recoil
// The half-span is the additive recoil form: baseAngle + baseAngle*recoil.
// `recoil` (iVar1+0x20) comes from the attribute holder returned by the virtual
// call at 964328 -- an OWNER lookup -- so it is supplied by the caller here. The
// decomp applies NO clamp; the bare expression is returned exactly. NO RGRandom
// draw on this path (it is the pure pre-draw math that FEEDS the scatter).
float Gun013::SpreadHalfSpan(float baseAngle, float recoil) {
    return baseAngle + baseAngle * recoil;
}

// FAITHFUL: Gun013__Attack @ game_full.c:964337 --
//   RGRandom__Range(*(int *)(this + 0x60), -fVar4, fVar4, 0);
// Exactly ONE RGRandom::Range(float) draw (max INCLUSIVE), symmetric about the
// aim, with fVar4 == SpreadHalfSpan(baseAngle, recoil). The owner adds this
// euler-Z offset to the muzzle direction and spawns the bullet (the truncated
// UnityEngine_Component__get_transform tail at 964339 is the OWNER's concern).
float Gun013::Scatter(float baseAngle, float recoil) {
    const float spread = SpreadHalfSpan(baseAngle, recoil);
    return m_Rng.Range(-spread, spread);
}

} // namespace Game
