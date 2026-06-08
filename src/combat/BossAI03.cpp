#include "combat/BossAI03.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: BossAI03__BossAngry @ game_full.c:438309 (field 0xe0 angry = 1;
// anim.set_speed(1.2f / 0x3f99999a); anim.SetBool("angry"/SL_6560, true)). The
// hp/max_hp < 0.5 gate is the standard boss GetHurt->BossAngry trigger; only the
// BossAngry effects (the brain-side writes) are modelled here.
void BossAI03::OnHurt(int hpAfter, int maxHp) {
    if (m_Angry || maxHp <= 0) {
        return;
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction) {
        m_Angry = true;             // angry(0xe0) = 1
        m_AnimSpeed = kAngryAnimSpeed; // anim.set_speed(1.2f)
        // owner: anim.SetBool("angry", true) (StringLiteral_6560).
    }
}

// FAITHFUL: BossAI03__Dizzy @ game_full.c:438810. Gate: only when !dead(0x38)
// does it set dizzy(0xa1) = 1 (+ anim.SetBool("run"/SL_6550, false)). No draw.
bool BossAI03::DizzyHit() {
    if (m_Dead) {
        return false; // gated out: no dizzy latch
    }
    m_Dizzy = true; // dizzy(0xa1) = 1
    return true;
}

// FAITHFUL: BossAI03__ChildDead @ game_full.c:438332. shooting(0x80) = 0 (+
// Invoke("CreateTransferGate", 2f / 0x40000000), RGMusicManager.StopBgm). No draw.
void BossAI03::ChildDead() {
    m_Shooting = false; // shooting(0x80) = 0
    // owner: Invoke("CreateTransferGate", 2f); GetInstance().StopBgm().
}

// FAITHFUL: BossAI03__RunReflection @ game_full.c:438034 (two Range(-1.0f, 1.0f)
// float draws built into a Vector2, normalized into move_direction; then
// anim.SetBool("run", true) -- owner). Draw order is x then y.
glm::vec2 BossAI03::WanderDirection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // 0xbf800000..0x3f800000
    const float ry = m_Rng.Range(kWanderMin, kWanderMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    return len > 0.0F ? glm::vec2(rx / len, ry / len)
                      : glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI03__ShootReflection @ game_full.c:438092. The single
// Range(0,100) draw is GATED: only when can_shoot(0x40) && !dead(0x38) &&
// !dizzy(0xa1) is it taken; otherwise no draw (stream lockstep). The
// roll->StartAtkNN jumptable was not recovered (indirect jump at 0x53ea2c);
// 5 equal buckets are a reconstruction. Returns kNoAttack (and draws nothing)
// when gated out.
int BossAI03::ChooseAttack() {
    if (!m_CanShoot || m_Dead || m_Dizzy) {
        return kNoAttack; // gated path: NO draw
    }
    const int roll = m_Rng.Range(0, kRollCeiling);
    int idx = roll / (kRollCeiling / kAttackCount) + 1; // 1..kAttackCount
    if (idx > kAttackCount) {
        idx = kAttackCount;
    }
    m_AtkIndex = idx;
    return idx;
}

// FAITHFUL: BossAI03__StartAtk02 @ game_full.c:438121. weapon_lock_target(0x1c)=1,
// shooting(0x80)=1 (+ PlayEffect(boss_clip[3]), SetTrigger SL_6554). No draw.
void BossAI03::StartAtk02() {
    m_WeaponLockTarget = true; // 0x1c = 1
    m_Shooting = true;         // 0x80 = 1
}

// FAITHFUL: BossAI03__StartAtk03 @ game_full.c:438159. shooting(0x80)=1 (+
// SetTrigger SL_6555, zero rigidbody velocity, PlayEffect(boss_clip[2])). No draw.
void BossAI03::StartAtk03() {
    m_Shooting = true; // 0x80 = 1
}

// FAITHFUL: BossAI03__StartAtk04 @ game_full.c:438209. shooting(0x80)=1,
// atk4_index(0xdc)=0 (+ PlayEffect(boss_clip[4]), SetTrigger SL_6556, zero
// move_direction). No draw.
void BossAI03::StartAtk04() {
    m_Shooting = true;  // 0x80 = 1
    m_Atk4Index = 0;    // 0xdc = 0
}

// FAITHFUL: BossAI03__StartAtk05 @ game_full.c:438255. shooting(0x80)=1 (+
// SetTrigger SL_6567). No draw.
void BossAI03::StartAtk05() {
    m_Shooting = true; // 0x80 = 1
}

// FAITHFUL: BossAI03__InAtk02 @ game_full.c:438466. shooting(0x80)=0 (+
// PlayEffect(boss_clip[0]), zero rigidbody velocity, spawn bullet02). No draw.
void BossAI03::InAtk02() {
    m_Shooting = false; // 0x80 = 0
}

// FAITHFUL: BossAI03__InAtk05 @ game_full.c:438684. The decomp body writes NO
// brain-side field (PlayEffect(boss_clip[0]), zero rigidbody velocity, spawn
// bullet05 are owner-side). No draw, no state write.
void BossAI03::InAtk05() {
    // intentionally empty: decomp performs no brain-side write here.
}

// FAITHFUL: BossAI03__EndAtk01 @ game_full.c:438421. weapon_lock_target(0x1c)=0,
// can_shoot(0x40)=1 (+ reset two hand localEulerAngles to 0), then a single
// Range(0,100) follow-up roll.
int BossAI03::EndAtk01() {
    m_WeaponLockTarget = false; // 0x1c = 0
    m_CanShoot = true;          // 0x40 = 1
    // owner: h1(0xcc)/h2(0xd0).localEulerAngles = (0,0,0).
    return m_Rng.Range(0, kRollCeiling);
}

// FAITHFUL: BossAI03__EndAtk02 @ game_full.c:438525. weapon_lock_target(0x1c)=0,
// can_shoot(0x40)=1 (+ reset two hand localEulerAngles to 0). The follow-up roll
// is GATED on angry(0xe0): only when angry does it draw Range(0,100); the calm
// path takes NO draw and falls through to a virtual tail-call (owner).
int BossAI03::EndAtk02() {
    m_WeaponLockTarget = false; // 0x1c = 0
    m_CanShoot = true;          // 0x40 = 1
    // owner: h1(0xcc)/h2(0xd0).localEulerAngles = (0,0,0).
    if (!m_Angry) {
        return -1; // calm path: NO draw, virtual tail-call to owner
    }
    return m_Rng.Range(0, kRollCeiling);
}

// Brain-side bookkeeping only: the decomp clears no atk_index field, so this
// resets only the brain's bucket and performs no field write / RNG draw the
// decomp omits.
void BossAI03::StopAttack() {
    m_AtkIndex = kNoAttack;
}

} // namespace Game
