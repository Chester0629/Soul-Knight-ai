#include "combat/BossAI08.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: BossAI08__Scout @ game_full.c:442527.
// Gate: cVar1 = dead(0x38); if dead==0, cVar1 = dizzy(0xa1). Live path
// (!dead && !dizzy) writes target_obj(0x7c) = 0 then get_transform (owner).
// Draws NO RNG on any path.
bool BossAI08::Scout(bool dead, bool dizzy) {
    if (!dead && !dizzy) {
        m_TargetCleared = true; // target_obj(0x7c) = null
        return true;
    }
    return false;
}

// FAITHFUL: BossAI08__RunReflection @ game_full.c:442552.
// Two Range(0xbf800000=-1f, 0x3f800000=1f) draws built into a Vector2 and
// normalized into move_direction (set_move_direction); the animator SetBool
// "run" is owner-side. Float Range is max-INCLUSIVE.
glm::vec2 BossAI08::WanderDirection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax);
    const float ry = m_Rng.Range(kWanderMin, kWanderMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    return len > 0.0F ? glm::vec2(rx / len, ry / len)
                      : glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI08__ShootReflection @ game_full.c:442610.
// Gate: can_shoot(0x40) != 0 && dead(0x38)==0 && dizzy(0xa1)==0 &&
// rg_random(0xc) != 0 -> single Range(0,100) draw (max EXCLUSIVE). On the gated
// path the decomp takes NO draw (tail-calls the unrecovered jumptable directly),
// so we consume no RNG to keep the stream in lockstep. The roll->StartAtkNN
// jumptable was not recovered (indirect jump at 0x0055477c); we return the raw
// roll and persist no atk_index (no decomp body writes one).
int BossAI08::ChooseAttack(bool canShoot, bool dead, bool dizzy) {
    if (canShoot && !dead && !dizzy && m_Rng.Seeded()) {
        return m_Rng.Range(0, kRollCeiling);
    }
    return -1; // gated: no draw taken
}

// FAITHFUL: BossAI08__GetHurt @ game_full.c:442723 -> BossAI08__BossAngry @
// game_full.c:442664. Gate awake(0x18) && !dead(0x38); base GetHurt is owner;
// on hp/max_hp < 0.5 && !angry(0xb0) -> BossAngry: angry(0xb0)=1 and
// shoot_cd(0x3c) *= 0.5 (animator speed=1.2 / "angry" bool are owner-side).
// UpDateBossHp is owner-side. Draws NO RNG.
void BossAI08::OnHurt(bool awake, bool dead, int hpAfter, int maxHp) {
    if (!awake || dead) {
        return; // hits before awake (0x18) or after dead (0x38) are ignored
    }
    if (m_Angry || maxHp <= 0) {
        return; // angry latch (0xb0); guard divide-by-zero
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction) {
        m_Angry = true;                 // angry(0xb0) = 1
        m_ShootCd *= kAngryShootCdScale; // shoot_cd(0x3c) *= 0.5
        // owner: anim.set_speed(1.2f) (0x3f99999a), anim.SetBool("angry", true).
    }
}

// FAITHFUL: BossAI08__StartAtk02 @ game_full.c:442643.
// `*(byte*)(param_1+7) = 1` -> weapon_lock_target(0x1c) = 1. The animator
// SetTrigger and the indirect-jumptable tail (0x005549a8) are owner-side.
void BossAI08::StartAtk02() {
    m_WeaponLockTarget = true;
}

// FAITHFUL: BossAI08__TrunWeaponLock @ game_full.c:442903.
// `*(byte*)(param_1+0x1c) = 0` -> weapon_lock_target(0x1c) = 0. The hand
// transform localEulerAngles reset is owner-side.
void BossAI08::TrunWeaponLock() {
    m_WeaponLockTarget = false;
}

// --- OWNER-side bodies (no brain logic; named for traceability) -------------
// BossAI08__Start            @442447 : get_transform (owner).
// BossAI08__FixedUpdate      @442460 : FixedUpdateSeed + rigidbody velocity
//                                      integration (move_direction * scalars);
//                                      conditionally resets awake(0x18) when
//                                      dead -- pure physics/transform, owner.
// BossAI08__ShootReflection tail     : indirect jumptable -> StartAtkNN (owner).
// BossAI08__BossAngry tail           : anim.set_speed / anim.SetBool (owner).
// BossAI08__FixedRotation    @442688 : Transform get/set position (owner).
// BossAI08__CreateTransferGate@442767: HideHpBar + ResourcesUtil.Load +
//                                      Instantiate (owner).
// BossAI08__InAtk01          @442801 : RGMusicManager.PlayEffect + CreateFireBall.
// BossAI08__CreateFireBall   @442830 : Instantiate<RGWeapon> (owner).
// BossAI08__InAtk02          @442859 : PlayEffect + Instantiate<RGWeapon> (owner).
// BossAI08__InAtk03          @442927 : PlayEffect + get_transform (owner).
// BossAI08__InAtk04          @442960 : PlayEffect + Instantiate<RGWeapon> (owner).
// BossAI08__Atk04Combo       @443026 : Instantiate<RGWeapon> (owner).
// (StartAtk01/StartAtk03/StartAtk04/ChildDead/OnGameStateChange/Awake: no
//  pure-logic body recovered; animator-trigger / base-delegate, owner.)

} // namespace Game
