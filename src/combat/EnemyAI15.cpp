#include "combat/EnemyAI15.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI15__Scout @ game_full.c:681062.
// Gate (lines 681072-681077): bVar2 = (dead==0); if bVar2 then cVar1 = dizzy;
// active only when (dead==0 && dizzy==0). When active: target_obj = null (0x7C,
// line 681078) then rg_random.Range(0, 10) (line 681081). The re-detect/re-target
// tail (FUN_010b7dcc, line 681084) is tail-call-truncated and not recoverable ->
// not modelled.
int EnemyAI15::Scout() {
    if (m_Dead || m_Dizzy) {
        return -1; // gated: no draw taken (keeps stream lockstep with original)
    }
    m_HasTarget = false; // target_obj = null (0x7C, line 681078)
    return m_Rng.Range(0, kScoutRerollCeiling); // line 681081 (max EXCLUSIVE)
}

// FAITHFUL: EnemyAI15__RunReflection @ game_full.c:681144.
// No dead/dizzy gate in the decomp body. Two rg_random.Range(-1f, 1f) draws
// (lines 681183, 681188), Vector2 built (FUN_00fa16ec, line 681189) and
// normalized (FUN_00fa1e04, line 681190), stored as move_direction
// (set_move_direction, line 681191). The target_obj position read (lines
// 681170-681178) is owner facing only. owner: anim.SetBool (line 681197).
glm::vec2 EnemyAI15::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 681183 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 681188 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI15__ShootReflection @ game_full.c:681202.
// Gate (lines 681212-681219): bVar2 = (dead==0); if bVar2 then cVar1 = dizzy;
// active only when (dead==0 && dizzy==0), else early return. When active:
// owner anim.SetTrigger (line 681224) then rg_random.Range(0, 100) (line 681227).
// The decomp writes NO state flags here; tail is truncated (FUN_010b7dcc).
int EnemyAI15::ShootReflection() {
    if (m_Dead || m_Dizzy) {
        return -1; // gated: early return, no draw taken (line 681218)
    }
    // owner: anim.SetTrigger("atk") on anim (0x5C) (line 681224).
    return m_Rng.Range(0, kShootRerollCeiling); // line 681227 (max EXCLUSIVE)
}

// FAITHFUL: EnemyAI15__FixedUpdate @ game_full.c:680925.
// The whole body is gated on awake (0x18, line 680954): not awake -> no-op (NO
// friction decay, NO velocity write, NO awake clear). When awake the branch is
// keyed on inertial_vel (0x44, line 680955). For inertial_vel <= 1.0 there is a
// dead (0x38) sub-branch (line 680956) that latches awake = 0 (line 680957) and
// the owner zeroes velocity, then tail-calls out (the steering block does not
// run); otherwise plain steering (680970-680994), no decay. The else branch
// (inertial_vel > 1.0, line 680996) composes the knockback velocity and decays
// inertial_vel by friction (0x50): *= friction (line 681026). Pure scalar/branch
// only; rigidbody velocity writes are owner.
EnemyAI15::StepResult EnemyAI15::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 680954): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // inertial_vel <= 1.0 (line 680955)
        if (m_Dead) {                            // dead (0x38) != 0 (line 680956)
            m_Awake = false; // awake = 0 (0x18, line 680957); owner zeroes vel.
            return StepResult::Dead; // tail-calls out; steering block not reached.
        }
        return StepResult::Steer; // plain steering block (680970-680994). No decay.
    }
    // else: inertial_vel > 1.0 (line 680996) -- knockback velocity composed; decay
    // inertial_vel by friction (line 681026).
    m_InertialVel *= friction;
    return StepResult::Knockback;
}

} // namespace Game
