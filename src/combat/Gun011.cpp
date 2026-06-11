#include "combat/Gun011.hpp"

namespace Game {

// FAITHFUL: Gun011__Attack @ game_full.c:964211 /
//           Gun011__CreateEndShootBullet @ game_full.c:964160 --
//   fVar4 = (float)VectorSignedToFloat(*(this+0x30));   // base spread angle
//   fVar4 = fVar4 + fVar4 * *(float *)(iVar1 + 0x20);   // base + base*recoil
// i.e. half = base * (1 + recoil). The owner component fetch (vtbl[0xf4]) that
// yields iVar1 is OWNER; we take the already-read recoil scalar. NO RGRandom draw.
float Gun011::ScatterHalfAngle(float baseAngle, float recoil) {
    return baseAngle + baseAngle * recoil;
}

// FAITHFUL: Gun011__Attack @ game_full.c:964217 /
//           Gun011__CreateEndShootBullet @ game_full.c:964166 --
//   RGRandom__Range(*(this+0x60), -fVar4, fVar4, 0);
// The max-INCLUSIVE float overload: exactly ONE draw, symmetric about 0.
float Gun011::ScatterAngle(float half) {
    return m_Rng.Range(-half, half);
}

// FAITHFUL: Gun011__Attack @ game_full.c:964176. Half-angle (zero draws) then the
// single symmetric scatter draw. The muzzle get_position(this+0x4c) spawn tail is
// truncated / OWNER. Exactly ONE RGRandom draw.
float Gun011::Attack(float baseAngle, float recoil) {
    return ScatterAngle(ScatterHalfAngle(baseAngle, recoil));
}

// FAITHFUL: Gun011__CreateEndShootBullet @ game_full.c:964134. Byte-for-byte
// identical to Attack in the decomp (same formula, same single Range draw); kept
// as a distinct entry point per the port FOCUS. Exactly ONE RGRandom draw.
float Gun011::CreateEndShootBullet(float baseAngle, float recoil) {
    return ScatterAngle(ScatterHalfAngle(baseAngle, recoil));
}

} // namespace Game
