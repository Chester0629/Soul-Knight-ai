#include "combat/Gun018.hpp"

namespace Game {

// FAITHFUL: Gun018__Attack @ game_full.c:965296-965297. The decomp computes
//   fVar4 = (float)VectorSignedToFloat(*(this+0x30));   // base aim angle
//   fVar4 = fVar4 + *(float *)(ownerObj + 0x20);        // + recoil
// This is an ADDITIVE spread (baseAngle + recoil), distinct from the
// multiplicative recoil scatter of the single-shot guns. The owner object is
// *(this+0x50) and the +0x20 recoil is read after its (*owner+0xf4) virtual
// call (OWNER). We sum the two scalars; no clamp is applied in the original.
float Gun018::AttackSpread(float baseAngle, float recoil) {
    return baseAngle + recoil;
}

// FAITHFUL: Gun018__Attack @ game_full.c:965302 --
//   RGRandom::Range(this+0x60, -fVar4, fVar4, 0).
// Exactly one float draw of the symmetric span, max-INCLUSIVE. The muzzle
// get_position(this+0x4c) spawn that follows is the truncated OWNER tail.
float Gun018::AttackScatter(float baseAngle, float recoil) {
    const float spread = AttackSpread(baseAngle, recoil);
    return m_Rng.Range(-spread, spread);
}

// FAITHFUL: Gun018__CreateBullet @ game_full.c:965319 --
//   if (-*(int *)(this+0x94) <= *(int *)(this+0x94))
// `-c <= c` holds exactly when c >= 0. Field 0x94 is a runtime counter the
// ctor does not initialise; the owner drives it across the pellet loop.
bool Gun018::PelletGatePasses(int counter) {
    return -counter <= counter;
}

// FAITHFUL: Gun018__CreateBullet @ game_full.c:965324 --
//   RGRandom::Range(this+0x60, -*(int *)(this+0x9c), *(int *)(this+0x9c), 0).
// One int draw of the symmetric deviation span, max-EXCLUSIVE, gated on the
// counter predicate. When the gate is closed the original takes no draw at all
// (the `if` body is skipped) -- we mirror that: ZERO draws, return 0. The
// bullet Instantiate that consumes the scattered angle is OWNER.
int Gun018::CreateBulletScatter(int counter) {
    if (!PelletGatePasses(counter)) {
        return 0; // gate closed: no RGRandom draw, exactly as the decomp.
    }
    return m_Rng.Range(-kBulletDeviation, kBulletDeviation);
}

} // namespace Game
