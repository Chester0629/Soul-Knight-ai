#include "combat/EnemyAI11.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI11__FixedUpdate @ game_full.c:679520.
// The whole body is gated on awake (0x18, line 679549): not awake -> no-op (NO
// decay, NO velocity write). When awake the outer branch splits on inertial_vel
// (0x44, line 679550). On the <= 1.0 branch the decomp first checks dead (0x38,
// line 679551): if dead it latches awake = 0 (line 679552), zeroes velocity, and
// exits via a get_transform tail-call truncation (the steering block does NOT run
// and there is NO decay). Otherwise it steers (block 679565-679589, no decay).
// On the > 1.0 branch (the else, line 679591) the knockback impulse is added to
// the steering and inertial_vel *= friction (0x50, line 679621) -- the ONLY decay
// site. Pure scalar/branch only; rigidbody velocity writes are owner concerns.
EnemyAI11::StepResult EnemyAI11::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 679549): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // inertial_vel <= 1.0 (line 679550)
        if (m_Dead) {                            // dead (0x38, line 679551)
            m_Awake = false; // awake = 0 (line 679552)
            // owner: zero rigidbody velocity (line 679561) then get_transform
            // tail-call truncation (line 679563): the steering block is skipped,
            // no decay.
            return StepResult::Dead;
        }
        // owner: velocity = move_direction * speed * (speed_rate + 1)
        // (lines 679565-679589). No decay on the steer path.
        return StepResult::Steer;
    }
    // inertial_vel > 1.0 (else, line 679591): knockback impulse added to steering
    // (lines 679592-679620), then decay (line 679621).
    m_InertialVel *= friction; // inertial_vel *= friction (0x50, line 679621)
    return StepResult::Knockback;
}

// FAITHFUL: EnemyAI11__Scout @ game_full.c:679629.
// Gate (lines 679639-679644): the decomp tests dizzy (0xA1) first; only when
// dizzy == 0 does it read dead (0x38). It runs only when not dizzy && not dead.
// When active: target_obj = null (0x7C, line 679645) then get_transform tail-call
// truncation (line 679647). This override takes NO rg_random draw (it does NOT
// reproduce the base Scout's Range(0,10)).
bool EnemyAI11::Scout() {
    if (m_Dizzy || m_Dead) {
        return false; // gated: no state change, no draw (keeps stream lockstep).
    }
    m_HasTarget = false; // target_obj = null (0x7C)
    return true;
}

// FAITHFUL: EnemyAI11__RunReflection @ game_full.c:679654.
// Two rg_random.Range(-1f, 1f) draws (lines 679674, 679679), Vector2 normalized
// (FUN_00fa16ec build + FUN_00fa1e04 normalize, lines 679680-679681), stored as
// move_direction (set_move_direction, line 679682).
// owner: anim.SetBool("...", true) at the tail (line 679688).
glm::vec2 EnemyAI11::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 679674 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 679679 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI11__ShootReflection @ game_full.c:679693.
// Gate (lines 679705-679710): the decomp tests dead (0x38) first; only when
// dead == 0 does it read dizzy (0xA1). It runs only when not dead && not dizzy.
bool EnemyAI11::ShootReflection(float &outShootCd, float shootCd) {
    if (m_Dead || m_Dizzy) {
        return false;
    }
    m_CanShoot = false;         // 0x40 = 0 (line 679711)
    m_AtkCount = kBurstCount;   // 0xC4 = 5 (line 679712)
    m_MoveDirection = glm::vec2(0.0F, 0.0F); // move_direction = Vector2.zero (line 679719)
    m_WeaponLockTarget = false; // 0x1C = 0 (line 679731)
    // owner: Invoke("...", shoot_cd@0x3C) (line 679720), anim.SetTrigger x2
    // (lines 679725, 679730).
    outShootCd = shootCd; // 0x3C delay -> burst-fire Invoke
    return true;
}

// FAITHFUL: FUN_007f7928 (the Invoke callback, r9 == 2 path) @ game_full.c:679796.
// atk_count = atk_count - 1. Models ONLY the recovered decrement; the dispatch
// condition (uninitialised r9 register) is not recoverable -> fabricationFlags.
// owner: re-Invoke + RGMusicManager.PlayEffect(atk1_cilp@0xB4) (lines 679797-679803).
int EnemyAI11::DrainBurst() {
    m_AtkCount = m_AtkCount - 1; // 0xC4-- (line 679796)
    return m_AtkCount;
}

} // namespace Game
