#include "combat/Gun001.hpp"

namespace Game {

// FAITHFUL: Gun001__Attack @ game_full.c:315778 --
//   fVar4 = (float)VectorSignedToFloat(uVar3, ...);   // uVar3 = *(param_1 + 0x30)
//   fVar4 = fVar4 + fVar4 * *(float *)(iVar1 + 0x20);  // iVar1 = holder subobject
// The recoil multiplier lives at +0x20 of the subobject reached through this+0x50
// and a virtual accessor (vtable+0xf4); that holder walk + null-checks are OWNER,
// so the caller hands us the resolved recoil float directly. No clamp in the
// decomp -- the multiply-add is reproduced verbatim. NO RGRandom draw here.
float Gun001::SpreadHalfAngle(float baseAngle, float recoil) {
    return baseAngle + baseAngle * recoil;
}

// FAITHFUL: Gun001__Attack @ game_full.c:315784 --
//   RGRandom__Range(*(param_1 + 0x60), -fVar4, fVar4, 0);
// Exactly one RGRandom::Range(float, float) draw (max INCLUSIVE), symmetric about
// 0 with the recoil-widened half-angle as the bound. The drawn offset is the
// scatter the owner applies before the bullet spawn (Singleton<PrefabPool>
// get_Inst tail = OWNER, truncated). This is the weapon's single, ordered draw.
float Gun001::ScatterAngle(float baseAngle, float recoil) {
    const float spread = SpreadHalfAngle(baseAngle, recoil);
    return m_Rng.Range(-spread, spread);
}

} // namespace Game
