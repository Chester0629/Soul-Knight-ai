#include "combat/BulletColor.hpp"

namespace Game {

// FAITHFUL: BulletColor__Start @ game_full.c:962850.
//   if (class_init_flag) {
//       value = UnityEngine.Random.Range(0, 6);   // one int, {0..5}, max EXCLUSIVE
//       switch (value) { case 0..5: get_transform(this); }  // owner colour write
//   }
// The Range(0, 6) draw is the GLOBAL Unity RNG (UnityEngine_Random__Range),
// modelled here by the injected RGRandom (same Xorshift128, same int
// max-EXCLUSIVE semantics) -- the same treatment BossAI06Child gives its global
// Range(0, 2). The static-init guard is IL2CPP class-init (always true at
// runtime), so the roll is always taken. Every switch case collapses to the same
// owner-side get_transform(this) tail; the per-index colour VALUE is not
// recoverable, so only the chosen INDEX is modelled.
int BulletColor::PickColorIndex() {
    const int value = m_Rng.Range(0, kColorCount); // one int draw, {0,1,2,3,4,5}
    m_ColorIndex = value;
    return value;
}

// FAITHFUL: BulletColor__FixedUpdate @ game_full.c:962903.
//   alive = (this+0x20 != 0);                 // RGBullet.awake @0x20
//   rot   = alive ? *(this+0x1c) : 0;         // RGBullet.rotate_angle @0x1C
//   if (!alive || rot == 0) return;           // gate: skip the rotate
//   get_transform(this);                      // owner: rotate transform
// Recoverable scalar = the gate predicate `alive && rotateAngle != 0`; the
// transform rotation is owner-side. NO RGRandom draw.
bool BulletColor::ShouldRotate(bool awake, int rotateAngle) {
    return awake && rotateAngle != 0;
}

} // namespace Game
