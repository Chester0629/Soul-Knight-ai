#include "combat/BossAI02.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: BossAI02__BossAngry @ game_full.c:437520 (once; field 0xd0 = 1,
// role_attribute.speed(+0x14) += 0.2, anim.speed = 1.2). The hp/max_hp < 0.5
// gate is the standard boss GetHurt->BossAngry trigger.
void BossAI02::OnHurt(int hpAfter, int maxHp) {
    if (m_Angry || maxHp <= 0) {
        return;
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction) {
        m_Angry = true;
        m_SpeedBonus += kAngrySpeedBonus; // role_attribute.speed += 0.2
        m_AnimSpeed = kAngryAnimSpeed;     // anim.set_speed(1.2f)
        // owner: anim.SetBool("angry", true) (StringLiteral_6560).
    }
}

// FAITHFUL: BossAI02__ShootReflection @ game_full.c:437471
// (rg_random.Range(0, 100) attack roll).
int BossAI02::ChooseAttack() {
    const int roll = m_Rng.Range(0, kRollCeiling);
    // The roll->InAtkNN jumptable was not recovered (indirect jump at 0x53b460,
    // "Too many branches"); 4 equal buckets are a reconstruction
    // (fabricationFlags). Like BossAI01, the bucket is returned but NOT
    // persisted: no decomp InAtkNN body writes an atk_index field.
    int idx = roll / (kRollCeiling / kAttackCount);
    if (idx >= kAttackCount) {
        idx = kAttackCount - 1;
    }
    return idx;
}

// FAITHFUL: BossAI02__RunReflection @ game_full.c:437433/437438
// (Range(-1,1) x2, normalized into move_direction).
glm::vec2 BossAI02::WanderDirection() {
    const float rx = m_Rng.Range(-1.0F, 1.0F);
    const float ry = m_Rng.Range(-1.0F, 1.0F);
    const float len = std::sqrt(rx * rx + ry * ry);
    return len > 0.0F ? glm::vec2(rx / len, ry / len)
                      : glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI02__InAtk01 @ game_full.c:437645/437658
// (shooting(0x80) = 1, weapon_lock_target(0x1c) = 0). No RNG draw.
void BossAI02::InAtk01() {
    m_Shooting = true;
    m_WeaponLockTarget = false;
}

// FAITHFUL: BossAI02__InAtk02 @ 437705 (shooting(0x80) = 1) then
// BossAI02__CreateBullet @ game_full.c:437736 (Range(-60, 60)).
int BossAI02::InAtk02() {
    m_Shooting = true; // shooting(0x80) = 1; decomp writes no atk_index/lock here
    return m_Rng.Range(kBulletAngleMin, kBulletAngleMax); // bullet angle/spread
}

// FAITHFUL: BossAI02__EndAtk02 @ game_full.c:437753/437765
// (shooting(0x80) = 0, can_shoot(0x40) = 1). No RNG draw.
void BossAI02::EndAtk02() {
    m_Shooting = false;
    m_CanShoot = true;
    // owner: reset hand localEulerAngles to 0, Invoke("TrunWeaponLock", 0.1f).
}

// FAITHFUL: BossAI02__EndAtk03 @ game_full.c:437843/437854/437860
// (weapon_lock_target(0x1c) = 0, can_shoot(0x40) = 1, then Range(0, 10)).
int BossAI02::EndAtk03() {
    m_WeaponLockTarget = false;
    m_CanShoot = true;
    return m_Rng.Range(0, kEndAtk03RollCeiling);
}

// FAITHFUL: BossAI02__InAtk04 @ game_full.c:437895 (Range(0, 3)). The decomp
// body writes no state field (PlayEffect is owner-side), so this draws RNG only.
int BossAI02::InAtk04() {
    return m_Rng.Range(0, kInAtk04RollCeiling);
}

// FAITHFUL: BossAI02__EndAtk04 @ game_full.c:437908/437919
// (weapon_lock_target(0x1c) = 0, can_shoot(0x40) = 1). No RNG draw.
void BossAI02::EndAtk04() {
    m_WeaponLockTarget = false;
    m_CanShoot = true;
}

} // namespace Game
