#include "combat/Gun004.hpp"

namespace Game {

// FAITHFUL: Gun004.Attack @ game_full.c:316295-316309 (FUN_003ed07c inner loop).
//   unaff_r5 = unaff_r5 + 1;                       // count this shot
//   if (unaff_r4[0x1b] <= unaff_r5) { ...end... }  // limit reached -> stop
//   if ((char)unaff_r4[0x1c] != '\0') break;       // interrupt flag -> stop
//   Gun004__CreateBullet();                        // else fire one bullet
// The increment happens BEFORE the limit compare, so the salvo fires exactly
// `count` bullets (counter 1..count). The end branch's anim stop (vtable+0x134),
// RGMusicManager.PlayEffect end-SFX, and the Invoke() re-pump are OWNER.
bool Gun004::ShouldFireShot(bool interrupted) {
    // unaff_r5 = unaff_r5 + 1
    m_ShotsFired += 1;
    // if (count(0x6c) <= unaff_r5) -> burst ends (no fire this pump).
    if (m_BurstCount <= m_ShotsFired) {
        return false;
    }
    // if ((char)flag(0x70) != 0) -> in-attack interrupt; break the burst.
    if (interrupted) {
        return false;
    }
    // else -> Gun004.CreateBullet(): the owner spawns one bullet and draws one
    // scatter angle via ShotScatterAngle.
    return true;
}

// FAITHFUL: Gun004.CreateBullet @ game_full.c:316365-316366:
//   fVar4 = VectorSignedToFloat(*(owner+0x30));    // base spread angle
//   fVar4 = fVar4 + fVar4 * *(float *)(iVar1+0x20);// + base*recoil
// `recoil` (component+0x20) is read off the bullet/RGBullet component fetched
// through the gameObject at owner+0x50 (vtable+0xf4); the owner supplies it.
// NO RGRandom draw on this line.
float Gun004::ShotSpread(float base, float recoil) {
    return base + base * recoil;
}

// FAITHFUL: Gun004.CreateBullet @ game_full.c:316371 --
//   RGRandom__Range(*(owner+0x60), -fVar4, fVar4, 0).
// ONE max-INCLUSIVE float draw from this weapon's stream per spawned bullet.
// The PrefabPool Instantiate / GetComponent<RGBullet> spawn tail (316372-316379)
// is OWNER; we model only the scatter draw that feeds the bullet's rotation.
float Gun004::ShotScatterAngle(float spread) {
    return m_Rng.Range(-spread, spread);
}

} // namespace Game
