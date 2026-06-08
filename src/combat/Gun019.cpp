#include "combat/Gun019.hpp"

namespace Game {

// FAITHFUL: Gun019__CreateBullet @ game_full.c:965364-965373 (burst-reschedule
// branch, taken when c_count < 0; gate at 965357: `x < -x` is true iff x < 0
// for signed int). The decomp increments index (owner+0x80)
// FIRST, then tests index < count (owner+0x78): on the in-range case it would
// Invoke("CreateBullet", delay) to re-schedule the next shot (leaving in_atk
// set); on the exhausted case it clears in_atk (owner+0x84) to stop the burst.
// The RGMusicManager.PlayEffect at the top of the branch and the
// MonoBehaviour.Invoke re-schedule are OWNER side-effects; we model only the
// counter/limit/flag transition. NO RGRandom draw on this path.
Gun019::BurstStep Gun019::BurstAdvance(int index, int count) {
    BurstStep step;
    step.index = index + 1;          // 965364: iVar1 = *(param+0x80) + 1
    if (step.index < count) {        // 965366: if (index < count)
        step.reschedule = true;      //          Invoke("CreateBullet", delay) (OWNER)
        step.inAtk = true;           //          burst stays armed
    } else {
        step.reschedule = false;
        step.inAtk = kInAtkStopped;  // 965371: *(param+0x84) = 0 (stop burst)
    }
    return step;
}

// FAITHFUL: Gun019__CreateBullet @ game_full.c:965383-965384 (main-shot branch,
// taken when c_count <= 0). spread = angle + angle*recoil = angle*(1 + recoil),
// where `angle` is the owner's base scatter field (owner+0x30, widened to float
// by VectorSignedToFloat) and `recoil` is a bullet-component float
// (component+0x20). This mirrors the canonical single-shot gun scatter shape
// (base + base*recoil; cf. Gun008::SweepHalfAngle). NO RGRandom draw here.
float Gun019::SpreadHalfAngle(float angle, float recoil) {
    return angle + angle * recoil;
}

// FAITHFUL: Gun019__CreateBullet @ game_full.c:965389 --
// RGRandom__Range(owner+0x60, -fVar4, fVar4, 0): ONE max-INCLUSIVE float draw,
// the symmetric per-shot scatter. Preserve exactly this single draw per main
// shot. The muzzle Transform.get_position bullet spawn (owner+0x4c) that follows
// is the OWNER's concern (truncated tail in the decomp).
float Gun019::ScatterAngle(float spread) {
    return m_Rng.Range(-spread, spread);
}

} // namespace Game
