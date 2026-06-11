#include "combat/Gun009.hpp"

namespace Game {

// FAITHFUL: Gun009__Attack @ game_full.c:963998 --
//   uVar3  = *(undefined4 *)(param_1 + 0x30);            // base spread angle
//   fVar4  = (float)VectorSignedToFloat(uVar3, ...);     // signed -> float
//   fVar4  = fVar4 + fVar4 * *(float *)(iVar1 + 0x20);   // angle + angle*recoil
// where iVar1 is the bullet-data object fetched by the virtual call at
// *(*(this+0x50))+0xf4 (OWNER). This is the canonical scatter half-angle
// baseAngle*(1+recoil); the decomp applies no clamp, so the sign carries through.
// NO RGRandom draw on this scalar path.
float Gun009::ComputeSpread(float baseAngle, float recoil) {
    return baseAngle + baseAngle * recoil;
}

// FAITHFUL: Gun009__Attack @ game_full.c:964021 --
//   RGRandom__Range(*(int *)(param_1 + 0x60), -fVar4, fVar4, 0);
// EXACTLY ONE RGRandom::Range(float,float) draw (max INCLUSIVE), symmetric about
// 0 with half-width = ComputeSpread(baseAngle, recoil). The result is the per-shot
// scatter angle the spawned bullet is rotated by; the PrefabPool.Inst spawn that
// consumes it (964025+) is the truncated OWNER tail.
float Gun009::RollScatter(float baseAngle, float recoil) {
    const float spread = ComputeSpread(baseAngle, recoil);
    return m_Rng.Range(-spread, spread);
}

} // namespace Game
