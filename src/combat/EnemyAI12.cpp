#include "combat/EnemyAI12.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI12__Scout @ game_full.c:680022.
// Gate (lines 680032-680037): active only while not dead (0x38) AND not dizzy
// (0xA1). When active: target_obj = null (0x7C, line 680038). The tail (line
// 680040) is a get_transform tail-call and is not modelled. NO RNG draw.
bool EnemyAI12::Scout() {
    const bool gated = m_Dead || m_Dizzy; // decomp: bVar2 = !dead, then && !dizzy
    if (gated) {
        return false;
    }
    m_HasTarget = false; // target_obj = null (0x7C, line 680038)
    return true;
}

// FAITHFUL: EnemyAI12__RunReflection @ game_full.c:680047.
// Two rg_random.Range(-1f, 1f) draws (lines 680067, 680072; 0xbf800000 = -1.0f,
// 0x3f800000 = 1.0f), Vector2 build (FUN_00fa16ec, line 680073) + normalize
// (FUN_00fa1e04, line 680074), stored as move_direction (set_move_direction,
// line 680075).
// owner: anim.SetBool("walk", true) at the tail (line 680081).
glm::vec2 EnemyAI12::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 680067 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 680072 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI12__ShootReflection @ game_full.c:680101.
// Gate (lines 680113-680118): active only while not dead (0x38) AND not dizzy
// (0xA1). When active: can_shoot = false (0x40, line 680119); move_direction =
// Vector2.zero (0x74, lines 680125-680126); weapon_lock_target = false (0x1C,
// line 680138). No RNG draws.
bool EnemyAI12::ShootReflection(float &outFireBallDelay, float shootCd) {
    const bool gated = m_Dead || m_Dizzy;
    if (gated) {
        return false;
    }
    m_CanShoot = false;                   // 0x40 = 0 (line 680119)
    m_MoveDirection = glm::vec2(0.0F, 0.0F); // set_move_direction(zero) (680125-680126)
    // owner: Invoke("CreateFireBall", shoot_cd@0x3C) (line 680127),
    // anim.SetTrigger(6616) (line 680132), anim.SetTrigger(6553) (line 680137).
    m_WeaponLockTarget = false;           // 0x1C = 0 (line 680138)
    outFireBallDelay = shootCd;           // 0x3C delay -> Invoke("CreateFireBall")
    return true;
}

// FAITHFUL: EnemyAI12__FixedUpdate @ game_full.c:679913.
// Outer gate awake (0x18, line 679942): not awake -> no-op (NO friction decay,
// NO velocity write). When awake, branch on inertial_vel (0x44, line 679943):
//   * inertial_vel <= 1.0:
//       - dead (0x38, line 679944): awake = 0 (0x18, line 679945), velocity
//         zeroed (owner, line 679954), then EARLY-RETURN (get_transform tail-call
//         line 679956). The steering block (679958-679982) does NOT run.
//       - else: normal steering (679958-679982). No decay (inertial_vel <= 1.0).
//   * inertial_vel > 1.0: knockback term added to steering velocity
//     (679985-680013), then inertial_vel *= friction (0x50, line 680014).
// Pure scalar/branch only; rigidbody velocity writes are owner.
EnemyAI12::StepResult EnemyAI12::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 679942): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // line 679943 (inertial_vel <= 1.0)
        if (m_Dead) {                            // line 679944
            m_Awake = false; // 0x18 = 0 (line 679945); owner zeroes velocity.
            return StepResult::Dead; // early-return (get_transform tail, line 679956).
        }
        return StepResult::Steer; // plain steering block (679958-679982), no decay.
    }
    // inertial_vel > 1.0: steering + knockback, then decay (line 680014).
    m_InertialVel *= friction; // 0x44 *= friction (0x50)
    return StepResult::Knockback;
}

} // namespace Game
