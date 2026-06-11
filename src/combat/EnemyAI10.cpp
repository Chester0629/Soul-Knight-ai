#include "combat/EnemyAI10.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI10__Scout @ game_full.c:679153.
// Gate (lines 679163-679168): cVar1 = dizzy (0xA1); bVar2 = (dizzy == 0); if
// bVar2 then cVar1 = dead (0x38); the body runs only when (bVar2 && cVar1 == 0),
// i.e. !dizzy && !dead. When active: target_obj = null (0x7C, line 679169). The
// tail get_transform (line 679171) "does not return" in the decomp -> not
// modelled. No RNG draws.
bool EnemyAI10::Scout() {
    if (m_Dizzy || m_Dead) {
        return false; // gated: no state change, no draw.
    }
    m_HasTarget = false; // target_obj = null (0x7C)
    // owner: get_transform tail (line 679171, truncated).
    return true;
}

// FAITHFUL: EnemyAI10__RunReflection @ game_full.c:679215.
// The leading target_obj position read (lines 679241-679248) only feeds the
// owner's facing. Two rg_random.Range(-1f, 1f) draws (0xbf800000=-1.0f,
// 0x3f800000=1.0f; lines 679254, 679259), Vector2 build + normalize
// (FUN_00fa16ec / FUN_00fa1e04, lines 679260-679261), stored as move_direction
// (set_move_direction, line 679262).
// owner: anim.SetBool("walk", true) at the tail (line 679268).
glm::vec2 EnemyAI10::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 679254 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 679259 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI10__ShootReflection @ game_full.c:679273.
// Gate (lines 679283-679290): cVar1 = dead (param_1[0xe] -> 0x38); bVar2 =
// (dead == 0); if bVar2 then cVar1 = dizzy (0xA1); guard returns when
// (!bVar2 || cVar1 != 0), i.e. runs only when !dead && !dizzy. When active:
// can_shoot = false (param_1+0x10 -> 0x40, line 679291).
bool EnemyAI10::ShootReflection(float &outOnAtkDelay, float shootCd) {
    if (m_Dead || m_Dizzy) {
        return false; // gated: no state change, no draw.
    }
    m_CanShoot = false; // 0x40 = 0 (line 679291)
    // owner: Invoke("OnAtk", shoot_cd@0x3C) (line 679292), anim.SetTrigger
    // (lines 679297-679299), vtable call (truncated jumptable, line 679302).
    outOnAtkDelay = shootCd; // 0x3C delay -> Invoke("OnAtk")
    return true;
}

// FAITHFUL: EnemyAI10__OnAtk @ game_full.c:679313.
// move_direction = Vector2.zero (get_zero + set_move_direction, lines
// 679329-679330) to freeze in place before the weapon spawns.
// owner: Instantiate weapon prefab (0xB0) then get_transform tail (lines
// 679336-679344, truncated). No RNG draws.
void EnemyAI10::OnAtk() {
    m_MoveDirection = glm::vec2(0.0F, 0.0F); // 0x74 = Vector2.zero
}

// FAITHFUL: EnemyAI10__GetHurt @ game_full.c:679449.
// if (temp_enemy == 0) (0x19, line 679456) -> base GetHurt (line 679457).
// else: if (dead == 0) (0x38, line 679460) -> owner shows UICanvas popup
// (truncated, line 679462); else nothing. This method takes no RNG draws and
// writes no fields itself.
EnemyAI10::HurtResult EnemyAI10::GetHurt() const {
    if (!m_TempEnemy) {
        return HurtResult::Base; // delegate to RGEController.GetHurt(damage, source)
    }
    if (!m_Dead) {
        return HurtResult::TempUi; // owner: UICanvas damage popup
    }
    return HurtResult::TempDead;
}

// FAITHFUL: EnemyAI10__FixedUpdate @ game_full.c:678932.
// Outer gate awake (0x18, line 678961): not awake -> no-op (NO decay, NO
// velocity write). When awake, inertial_vel (0x44) selects the path:
//   - inertial_vel <= 1.0 (line 678962):
//       - dead (0x38, line 678963): awake = 0 (line 678964), zero velocity
//         (lines 678965-678973), then the get_transform tail "does not return"
//         (line 678975) -> the steering block below is unreachable -> DeadStop.
//       - not dead: steering composition (move_direction * speed factors, lines
//         678977-679001), no decay -> Steer.
//   - inertial_vel > 1.0 (else, lines 679003-679034): steering + force_direction
//     * inertial_vel composition, then inertial_vel *= friction (0x50, line
//     679033) -> Knockback. Decay happens ONLY on this branch.
// Pure scalar/branch only; rigidbody velocity writes are owner concerns.
EnemyAI10::StepResult EnemyAI10::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 678961): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // inertial_vel <= 1.0 (line 678962)
        if (m_Dead) {                            // dead (0x38, line 678963)
            m_Awake = false; // 0x18 = 0 (line 678964); owner zeroes velocity.
            return StepResult::DeadStop;
        }
        // owner: steering velocity composition (lines 678977-679001). No decay.
        return StepResult::Steer;
    }
    // inertial_vel > 1.0 (else, lines 679003-679034): steering + force compose,
    // then decay.
    m_InertialVel *= friction; // line 679033 (0x50). Owner composes velocity.
    return StepResult::Knockback;
}

} // namespace Game
