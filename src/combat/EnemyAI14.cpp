#include "combat/EnemyAI14.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI14__Scout @ game_full.c:680630.
// Gate (lines 680640-680644): active only when not dead (0x38) AND not dizzy
// (0xA1). When active, the single state write is target_obj = null (0x7C, line
// 680646). The get_transform tail (line 680648) is owner-only. NO RNG draws.
bool EnemyAI14::Scout() {
    if (m_Dead || m_Dizzy) {
        return false; // gated: no state write
    }
    m_HasTarget = false; // target_obj = null (0x7C)
    return true;
}

// FAITHFUL: EnemyAI14__RunReflection @ game_full.c:680655.
// Two rg_random.Range(-1f, 1f) draws (lines 680694, 680699), Vector2 normalized
// (FUN_00fa16ec build + FUN_00fa1e04 normalize, lines 680700-680701), stored as
// move_direction (set_move_direction, line 680702). No dead/dizzy gate: the
// decomp draws unconditionally. owner: anim.SetBool("walk", true) (line 680708).
glm::vec2 EnemyAI14::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 680694 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 680699 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI14__ShootReflection @ game_full.c:680713.
// Gate (lines 680723-680727): active only when not dead (0x38) AND not dizzy
// (0xA1). When active: can_shoot = false (0x40, line 680729), weapon_lock_target
// = false (0x1C, line 680730). NO RNG draws.
bool EnemyAI14::ShootReflection(float &outAtkDelay, float &outEndDelay,
                                float shootCd) {
    if (m_Dead || m_Dizzy) {
        return false;
    }
    m_CanShoot = false;        // 0x40 = 0 (line 680729)
    m_WeaponLockTarget = false; // 0x1C = 0 (line 680730)
    // owner: Invoke("OnAtk", shoot_cd@0x3C) (line 680731), SetTrigger (line
    // 680733), Invoke("EndAtk", 0.5f@0x3f000000) (line 680734).
    outAtkDelay = shootCd; // 0x3C delay -> OnAtk
    outEndDelay = 0.5F;     // 0x3f000000 literal -> EndAtk
    return true;
}

// FAITHFUL: EnemyAI14__OnAtk @ game_full.c:680747.
// Gate (lines 680758-680760): returns immediately while dead (0x38). When active,
// the only state write is move_direction = Vector2.zero (0x74, lines 680766-
// 680767). owner: Instantiate<RGWeapon>(bullet01) + GetComponent<RGELaser> (the
// laser beam, lines 680773-680782). NO RNG draws.
bool EnemyAI14::OnAtk() {
    if (m_Dead) {
        return false;
    }
    m_MoveDirection = glm::vec2(0.0F, 0.0F); // Vector2.zero (0x74)
    return true;
}

// FAITHFUL: EnemyAI14__EndAtk @ game_full.c:680787.
// ReMoveLaser (owner) then weapon_lock_target = true (0x1C, line 680791; the
// decomp writes *(byte*)(param_1 + 7) where param_1 is int*, i.e. byte 0x1C).
// The trailing indirect call (line 680794) is owner/virtual dispatch. NO gate,
// NO RNG.
void EnemyAI14::EndAtk() {
    // owner: ReMoveLaser() tears down the active beam child (line 680790).
    m_WeaponLockTarget = true; // 0x1C = 1 (line 680791)
}

// FAITHFUL: EnemyAI14__FixedUpdate @ game_full.c:680521.
// The whole body is gated on awake (0x18, line 680550): not awake -> no-op (NO
// friction decay, NO awake clear). When awake, branch on inertial_vel (0x44):
//   <= 1.0 (line 680551 true):
//     dead (0x38) != 0 (line 680552): awake = 0 (line 680553), zero velocity
//       (owner), early-return via get_transform tail (line 680564) -> Dead.
//     else: normal steering (680566-680590), NO decay -> Steer.
//   > 1.0 (line 680551 false): knockback steering (680593-680621) then
//     inertial_vel *= friction (0x50, line 680622) -> Knockback.
// Pure scalar/branch only; rigidbody velocity writes are owner.
EnemyAI14::StepResult EnemyAI14::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 680550): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // inertial_vel <= 1.0 (line 680551)
        if (m_Dead) {
            // dead sub-branch (line 680552): clear awake, zero velocity (owner),
            // then early-return (get_transform tail, line 680564) before steering.
            m_Awake = false; // 0x18 = 0 (line 680553)
            return StepResult::Dead;
        }
        return StepResult::Steer; // plain steering block (680566-680590), no decay.
    }
    // inertial_vel > 1.0: knockback steering (680593-680621) then decay.
    m_InertialVel *= friction; // line 680622
    return StepResult::Knockback;
}

} // namespace Game
