#include "combat/BossAI11.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: BossAI11__FixedUpdate @ game_full.c:956676.
// Gate: awake(0x18) && !shooting(0x80). The only brain-visible field write is
// the dead-recovery branch (dead(0x38) -> awake = false, then zero velocity +
// get_transform owner-side); the move-velocity composition
// (move_direction * speed * (friction(0x14 on role_attribute) + 1)) is
// rigidbody work, left to the owner. No RNG draw.
bool BossAI11::FixedUpdate() {
    if (!m_Awake) {
        return false;
    }
    if (m_Shooting) {
        return false; // (char)param_1[0x20] != 0 -> skip the move block
    }
    if (m_Dead) {
        m_Awake = false; // *(undefined1 *)(param_1 + 6) = 0  (awake at 0x18)
        return true;     // owner: zero rigidbody velocity + get_transform
    }
    // owner: rigidbody.velocity = move_direction * speed * (friction + 1).
    return true;
}

// FAITHFUL: BossAI11__Scout @ game_full.c:956747.
// Gate: !dizzy(0xa1) && !dead(0x38) -> target_obj(0x7c) = null (then transform
// read, owner-side). No RNG draw.
bool BossAI11::Scout() {
    if (m_Dizzy || m_Dead) {
        return false;
    }
    m_HasTarget = false; // *(undefined4 *)(param_1 + 0x7c) = 0
    return true;
}

// FAITHFUL: BossAI11__RunReflection @ game_full.c:956772.
// Two RGRandom::Range(-1.0f, 1.0f) draws (0xbf800000 / 0x3f800000), normalized
// into move_direction; anim.SetBool("walk") is owner-side. Draw order: x, y.
glm::vec2 BossAI11::WanderDirection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // first draw
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // second draw
    const float len = std::sqrt(rx * rx + ry * ry);
    return len > 0.0F ? glm::vec2(rx / len, ry / len)
                      : glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI11__ShootReflection @ game_full.c:956830.
// Gate: can_shoot(0x40) && !dead(0x38) && !dizzy(0xa1). Open -> one
// Range(0, 100) draw; the roll->InAtkNN jumptable was not recovered (indirect
// jump at 0x00ad4684), so only the draw is modelled. Closed -> NO draw.
int BossAI11::ShootReflection() {
    if (!m_CanShoot) {
        return -1; // (char)param_1[0x10] == 0 -> no draw
    }
    if (m_Dead || m_Dizzy) {
        return -1; // dead(0xe)/dizzy(0xa1) set -> gate closed, no draw
    }
    return m_Rng.Range(0, kRollCeiling); // RGRandom__Range(rg_random, 0, 100)
}

// FAITHFUL: BossAI11__GetHurt @ game_full.c:956898 -> BossAI11__BossAngry @
// game_full.c:956940. Gates: awake(0x18) and !dead(0x38). Angry trigger:
// hp(0x1c)/max_hp(0x18) < 0.5 && !angry(0xd1). BossAngry: angry(0xd1) = 1,
// shoot_cd(0x3c) *= 0.5; anim.set_speed(1.2f) + SetBool("angry") owner-side.
// No RNG draw.
void BossAI11::OnHurt(int hpAfter, int maxHp) {
    if (!m_Awake) {
        return; // *(char *)(param_1 + 0x18) == 0 -> ignore
    }
    if (m_Dead) {
        return; // *(char *)(param_1 + 0x38) != 0 -> ignore
    }
    if (maxHp <= 0) {
        return; // guard the divide (fVar3 / fVar2)
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction && !m_Angry) {
        // BossAI11__BossAngry:
        m_Angry = true;                    // *(undefined1 *)(param_1 + 0xd1) = 1
        m_ShootCd *= kAngryShootCdScale;   // *(float *)(param_1 + 0x3c) *= 0.5
        m_AnimSpeed = kAngryAnimSpeed;     // anim.set_speed(1.2f) (owner mirror)
        // owner: anim.SetBool("angry", true) (StringLiteral_6560).
    }
    // owner: BossInfo.UpDateBossHp(boss_info, hp, max_hp).
}

// FAITHFUL: BossAI11__InAtk03 @ game_full.c:957038.
// *(undefined1 *)(param_1 + 0x1c) = 1 (weapon_lock_target); then
// Instantiate<RGWeapon> + GetComponent owner-side. No RNG draw.
void BossAI11::InAtk03() {
    m_WeaponLockTarget = true;
}

// FAITHFUL: BossAI11__Dizzy @ game_full.c:957124.
// Gate: !dead(0x38) -> dizzy(0xa1) = 1 (then anim.SetBool("walk", false)
// owner-side). No RNG draw.
bool BossAI11::MakeDizzy() {
    if (m_Dead) {
        return false; // *(char *)(param_1 + 0x38) != 0 -> no-op
    }
    m_Dizzy = true; // *(undefined1 *)(param_1 + 0xa1) = 1
    return true;
}

} // namespace Game
