#include "combat/EnemyAI07.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI07___ctor @ game_full.c:677681.
// Sets need_tap (0xAC) = 1 before delegating to RGEController___ctor. The base
// ctor wiring (rg_random, role_attribute, etc.) is the owner's concern.
EnemyAI07::EnemyAI07() {
    m_NeedTap = true; // 0xAC = 1 (line 677684)
}

// FAITHFUL: EnemyAI07__Scout @ game_full.c:677831.
// Gate (lines 677841-677846): cVar1 = dead (0x38); bVar2 = (dead == 0); if bVar2
// then cVar1 = dizzy (0xA1); the active branch requires (bVar2 && cVar1 == 0),
// i.e. !dead && !dizzy. When active: target_obj = 0 (0x7C, line 677847) then, if
// rg_random != 0, rg_random.Range(0, 10) (line 677850). The re-detect/re-target
// tail (FUN_010b7dcc, line 677853) is tail-call-truncated and not modelled.
int EnemyAI07::Scout() {
    if (m_Dead || m_Dizzy) {
        return -1; // gated: no draw taken (keeps stream lockstep with original)
    }
    m_HasTarget = false; // target_obj = null (0x7C)
    return m_Rng.Range(0, kScoutRerollCeiling); // line 677850 (max EXCLUSIVE)
}

// FAITHFUL: EnemyAI07__RunReflection @ game_full.c:677896.
// Two rg_random.Range(-1f, 1f) draws (lines 677935, 677940), Vector2 built
// (FUN_00fa16ec, line 677941) and normalized (FUN_00fa1e04, line 677942), stored
// as move_direction (set_move_direction, line 677943). NOT dead/dizzy gated: the
// decomp draws unconditionally. The op_Inequality(target_obj) position read at
// the head (lines 677922-677929) only feeds the owner's facing.
// owner: Animator.SetBool(StringLiteral_6550, 1) at the tail (line 677949).
glm::vec2 EnemyAI07::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 677935 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 677940 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI07__ShootReflection @ game_full.c:677954 (attack selection).
// Gate (lines 677964-677969): same !dead && !dizzy pattern as Scout. When active:
// if rg_random != 0, rg_random.Range(0, 100) (line 677972) -- the attack-selection
// roll. The branch mapping the roll to Atk1/Atk2/Atk3 is tail-call-truncated
// (FUN_010b7dcc, line 677975) and is NOT modelled.
int EnemyAI07::ShootReflection() {
    if (m_Dead || m_Dizzy) {
        return -1; // gated: no draw taken
    }
    return m_Rng.Range(0, kAttackRollCeiling); // line 677972 (max EXCLUSIVE)
}

// FAITHFUL: EnemyAI07__Atk1 @ game_full.c:678028.
// in_atk1 = 1 (0xAD, line 678038), then move_direction = Vector2.zero (0x74,
// get_zero line 678044 -> set_move_direction line 678045). No gate, no RNG.
void EnemyAI07::Atk1() {
    m_InAtk1 = true;                       // 0xAD = 1 (line 678038)
    m_MoveDirection = glm::vec2(0.0F, 0.0F); // Vector2.zero (lines 678044-678045)
}

// FAITHFUL: EnemyAI07__FixedUpdate @ game_full.c:677722.
// The whole body is gated on awake (0x18, line 677751): not awake -> no-op (NO
// friction decay, NO velocity write). When awake:
//   - inertial_vel (0x44) <= 1.0 (line 677752):
//       - dead (0x38) != 0 (line 677753): awake = 0 (line 677754), zero velocity
//         (FUN_00fa16ec line 677758 -> set_velocity line 677763), return.
//       - else: plain steering block (677767-677791); no friction decay.
//   - else (inertial_vel > 1.0): knockback block, inertial_vel *= friction (0x50)
//     (line 677823). Pure scalar/branch only; rigidbody velocity writes are owner.
EnemyAI07::StepResult EnemyAI07::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 677751): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // line 677752: inertial_vel <= 1.0
        if (m_Dead) {                            // line 677753: dead branch
            m_Awake = false;                     // awake = 0 (line 677754)
            // owner: zero rigidbody velocity (lines 677758-677763), then return.
            return StepResult::Dead;
        }
        // owner: plain steering velocity composition (lines 677767-677791).
        return StepResult::Steer;                // no friction decay on this path
    }
    // inertial_vel > 1.0 (else branch, line 677793): knockback replaces steering.
    m_InertialVel *= friction;                   // 0x44 *= 0x50 (line 677823)
    return StepResult::Knockback;
}

} // namespace Game
