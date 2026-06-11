#include "combat/EnemyAI04.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI04__Scout @ game_full.c:677234.
// Gate (lines 677244-677249): cVar1 = dead (0x38); bVar2 = (dead == 0); if dead
// then also read dizzy (0xA1); the body runs only when (dead == 0 && dizzy == 0).
// When active: target_obj = null (0x7C, line 677250) then rg_random.Range(0, 10)
// (line 677253). The re-detect/re-target tail (FUN_010b7dcc, line 677256) is
// tail-call-truncated and not recoverable -> not modelled.
int EnemyAI04::Scout() {
    if (m_Dead || m_Dizzy) {
        return -1; // gated: no draw taken (keeps stream lockstep with original)
    }
    m_HasTarget = false; // target_obj = null (0x7C, line 677250)
    return m_Rng.Range(0, kScoutRerollCeiling); // line 677253 (max EXCLUSIVE)
}

// FAITHFUL: EnemyAI04__RunReflection @ game_full.c:677281.
// Two rg_random.Range(-1f, 1f) draws (lines 677320, 677325; 0xbf800000 = -1.0f,
// 0x3f800000 = 1.0f), Vector2 build (FUN_00fa16ec, line 677326) then normalize
// (FUN_00fa1e04, line 677327), stored as move_direction (set_move_direction,
// line 677328). owner: anim.SetBool(..., true) at the tail (line 677334).
glm::vec2 EnemyAI04::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 677320 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 677325 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI04__FixedUpdate @ game_full.c:677125.
// awake gate (0x18, line 677154): not awake -> complete no-op (no decay, no
// velocity write, no awake clear). When awake the tree splits on inertial_vel
// (0x44) <= 1.0 (line 677155). Inside that branch dead (0x38, line 677156) is a
// TERMINAL "dead stop": it sets awake = 0 (line 677157, the only state write this
// method performs), the owner zeroes the rigidbody, and the steering block does
// NOT run (get_transform tail-call does not return). The not-dead case steers
// (block 677170-677194, owner). The else branch (inertial_vel > 1.0, line 677196)
// steers + adds force_direction * inertial_vel (owner), then decays
// inertial_vel *= friction (0x50, line 677226) -- the ONLY decaying path. Zero
// rng draws. Pure scalar/branch only; rigidbody velocity writes are owner.
EnemyAI04::StepResult EnemyAI04::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 677154): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // line 677155
        if (m_Dead) {                            // line 677156
            m_Awake = false; // awake = 0 (line 677157); terminal, owner zeroes vel.
            return StepResult::DeadStop;
        }
        return StepResult::Steer; // plain steering (block 677170-677194).
    }
    // else: inertial_vel > 1.0 (line 677196). owner adds force_direction *
    // inertial_vel; then decay (line 677226).
    m_InertialVel *= friction;
    return StepResult::Knockback;
}

} // namespace Game
