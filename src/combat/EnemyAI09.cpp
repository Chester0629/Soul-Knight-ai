#include "combat/EnemyAI09.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI09__Scout @ game_full.c:678641.
// Gate (lines 678651-678656): bVar2 = (dizzy 0xA1 == 0); if bVar2 then
// cVar1 = dead (0x38); body runs only when (bVar2 && cVar1 == 0), i.e.
// !dizzy && !dead. When active: target_obj = null (0x7C, line 678657). The
// re-detect/re-target tail (get_transform, line 678659) is tail-call-truncated
// and not recoverable -> not modelled. NO RNG draw.
bool EnemyAI09::Scout() {
    if (m_Dizzy || m_Dead) {
        return false; // gated: target untouched.
    }
    m_HasTarget = false; // target_obj = null (0x7C)
    return true;
}

// FAITHFUL: EnemyAI09__RunReflection @ game_full.c:678666.
// Two rg_random.Range(-1f, 1f) draws (lines 678705, 678710), Vector2 normalized
// (FUN_00fa16ec build + FUN_00fa1e04 normalize, lines 678711-678712), stored as
// move_direction (set_move_direction, line 678713).
// owner: anim.SetBool("run", true) at the tail (line 678719). The preceding
// target_obj position read (lines 678692-678699) is owner facing, not modelled.
glm::vec2 EnemyAI09::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 678705 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 678710 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI09__ShootReflection @ game_full.c:678724.
// Gate (lines 678734-678741): bVar2 = (dead 0x38 == 0); if bVar2 then
// cVar1 = dizzy (0xA1); returns when (!bVar2 || cVar1 != 0), i.e. dead || dizzy.
// When active: can_shoot = false (0x40, line 678742). NO RNG draw.
bool EnemyAI09::ShootReflection(float &outShootCd, float shootCd) {
    if (m_Dead || m_Dizzy) {
        return false;
    }
    m_CanShoot = false; // 0x40 = 0 (line 678742)
    // owner: Invoke("ShootReflection", shoot_cd@0x3C) (line 678743),
    // anim.SetBool (line 678746).
    outShootCd = shootCd; // 0x3C delay -> ShootReflection re-fire
    return true;
}

// FAITHFUL: EnemyAI09__FixedUpdate @ game_full.c:678509.
// Gated on awake (0x18, line 678538): not awake -> no-op (NO decay, NO velocity
// write). When awake the branch is on inertial_vel (0x44, line 678539):
//   - inertial_vel <= 1.0 && dead (0x38, line 678540): "dead stop" -- awake is
//     cleared (0x18 = 0, line 678541) and velocity zeroed (owner). No decay.
//   - inertial_vel <= 1.0 && not dead: normal steering (lines 678554-678578).
//     No decay.
//   - inertial_vel > 1.0 (line 678580): knockback impulse composited onto
//     steering and inertial_vel *= friction (0x50, line 678610). No early return.
// Pure scalar/branch only; rigidbody velocity writes are owner.
EnemyAI09::StepResult EnemyAI09::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 678538): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // inertial_vel <= 1.0 (line 678539)
        if (m_Dead) {                            // dead branch (line 678540)
            m_Awake = false; // 0x18 = 0 (line 678541); owner zeroes velocity.
            return StepResult::DeadStop; // no decay.
        }
        return StepResult::Steer; // plain steering (678554-678578); no decay.
    }
    // inertial_vel > 1.0 (line 678580): knockback composited + decay (line 678610).
    m_InertialVel *= friction;
    return StepResult::Knockback;
}

// FAITHFUL: EnemyAI09__GetForce @ game_full.c:678815.
// The base RGEController.GetForce (impulse magnitude clamp to 28) runs only when
// can_hit (0xB1) is false (line 678818); while can_hit is true force is ignored.
bool EnemyAI09::ShouldApplyForce() const {
    return !m_CanHit; // 0xB1 == 0 -> run base GetForce (line 678818).
}

} // namespace Game
