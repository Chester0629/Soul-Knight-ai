#include "combat/EnemyAI03.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI03__Scout @ game_full.c:676878.
// Gate (lines 676888-676893): skip while dead (0x38) or dizzy (0xA1). When
// active: target_obj = null (0x7C, line 676894) then rg_random.Range(0, 10)
// (line 676897). The re-detect/re-target tail (FUN_010b7dcc, line 676900) is
// tail-call-truncated and not recoverable -> not modelled.
int EnemyAI03::Scout() {
    if (m_Dead || m_Dizzy) {
        return -1; // gated: no draw taken (keeps stream lockstep with original)
    }
    m_HasTarget = false; // target_obj = null (0x7C)
    return m_Rng.Range(0, kScoutRerollCeiling); // line 676897 (max EXCLUSIVE)
}

// FAITHFUL: EnemyAI03__RunReflection @ game_full.c:676907.
// Two rg_random.Range(-1f, 1f) draws (lines 676946, 676951), Vector2 normalized
// (FUN_00fa16ec build + FUN_00fa1e04 normalize, lines 676952-676953), stored as
// move_direction (set_move_direction, line 676954).
// owner: anim.SetBool("walk", true) at the tail (line 676960).
glm::vec2 EnemyAI03::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 676946 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 676951 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI03__ShootReflection @ game_full.c:676965.
// Gate (lines 676978-676983): skip while dead (0x38) or dizzy (0xA1).
bool EnemyAI03::ShootReflection(float &outShootTime, float &outShootCd,
                                float shootTime, float shootCd) {
    if (m_Dead || m_Dizzy) {
        return false;
    }
    m_CanShoot = false; // 0x40 = 0 (line 676984)
    m_Shooting = true;  // 0x80 = 1 (line 676985)
    if (!m_LockInShooting) {     // 0xB1 == 0 (line 676986)
        m_WeaponLockTarget = false; // 0x1C = 0 (line 676987)
    }
    // owner: zero rigidbody velocity (lines 676995-677000), RGEHand.SetAttack(1)
    // (line 677005), Invoke("StopShooting", shoot_time@0xAC) (line 677006),
    // Invoke("ShootReflection", shoot_cd@0x3C) (line 677007).
    outShootTime = shootTime; // 0xAC delay -> StopShooting
    outShootCd = shootCd;      // 0x3C delay -> ShootReflection re-fire
    return true;
}

// FAITHFUL: EnemyAI03__StopShooting @ game_full.c:677015.
void EnemyAI03::StopShooting() {
    m_Shooting = false;        // 0x80 = 0 (line 677018)
    m_WeaponLockTarget = true; // 0x1C = 1 (line 677019)
    // owner: RGEHand.SetAttack(0) on hand (0x6C) (line 677024).
}

// FAITHFUL: EnemyAI03__Dizzy @ game_full.c:677030.
// Gate (line 677037): only while not dead (0x38).
bool EnemyAI03::ApplyDizzy() {
    if (m_Dead) {
        return false;
    }
    // owner: HitBack() flash (line 677038).
    m_Dizzy = true; // 0xA1 = 1 (line 677039)
    // owner: anim.SetBool("walk", false) (line 677045).
    return true;
}

// FAITHFUL: EnemyAI03__FixedUpdate @ game_full.c:676754.
// The whole body is gated on awake (0x18, line 676785): not awake -> no-op
// (NO friction decay, NO velocity write). When awake, the not-frozen gate (line
// 676790: cVar1 == 0 || param_2 == 0, i.e. !stand_in_shooting || !shooting)
// guards the rest. stand_in_shooting (0xB0) && shooting (0x80) is "frozen": the
// whole block is skipped -> no steer, no decay. Inside the not-frozen block, the
// knockback branch (line 676791: 1.0 < inertial_vel) decays inertial_vel by
// friction (0x50): *= friction (line 676825) and EARLY-RETURNS (line 676826),
// so the plain steering block (676846-676870) does NOT run. Otherwise normal
// steering applies. Pure scalar/branch only; rigidbody velocity writes are owner.
EnemyAI03::StepResult EnemyAI03::FixedUpdateStep(bool awake, float friction) {
    if (!awake) {
        return StepResult::Asleep; // awake gate (line 676785): nothing happens.
    }
    const bool frozen = m_StandInShooting && m_Shooting; // gate 676790 inverse.
    if (frozen) {
        return StepResult::Frozen; // not-frozen block skipped: no steer, no decay.
    }
    if (m_InertialVel > kKnockbackThreshold) { // 1.0 < inertial_vel (line 676791)
        // knockback replaces steering: decay (line 676825) then early-return
        // (line 676826) before the plain steering block.
        m_InertialVel *= friction;
        return StepResult::Knockback;
    }
    return StepResult::Steer; // plain steering block (676846-676870).
}

} // namespace Game
