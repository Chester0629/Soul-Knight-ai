#include "combat/EnemyAI13.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI13__Scout @ game_full.c:680356.
// Sets target_obj = null (0x7C, line 680363). The tail is a get_transform
// tail-call (Subroutine does not return, line 680365) -> owner concern, not
// modelled. NO rg_random call in this body.
void EnemyAI13::Scout() {
    m_HasTarget = false; // target_obj = 0 (0x7C, line 680363)
    // owner: Component.get_transform(this) (line 680365).
}

// FAITHFUL: EnemyAI13__RunReflection @ game_full.c:680370.
// GATE (line 680391): can_hit (0xAC) == 0 -> Invoke("RunReflection", scout_rate
// 0x98) and return WITH NO draw (line 680392). When can_hit != 0: two
// rg_random.Range(-1f, 1f) draws (lines 680413, 680418), Vector2 built
// (FUN_00fa16ec) + normalized (FUN_00fa1e04), stored as move_direction
// (set_move_direction, line 680421).
// owner: target_obj position read for facing (lines 680400-680408) and
// Animator.SetBool("reflect") (line 680427).
bool EnemyAI13::RunReflection(glm::vec2 &outDir) {
    if (!m_CanHit) {
        // owner: Invoke("RunReflection", scout_rate) (line 680392). NO draw taken.
        return false;
    }
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 680413 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 680418 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    outDir = m_MoveDirection;
    return true;
}

// FAITHFUL: EnemyAI13__ChildDead @ game_full.c:680467.
// Once-only latch (line 680476): boom_light (0xAD) != 0 -> return, detonate
// nothing. First call: boom_light = 1 (line 680479), StartBoom(boom 0xB0)
// (line 680484); secondBoom (0xB4) is StartBoom'd only when present
// (op_Implicit == 1, line 680490 -> StartBoom line 680498). NO RNG draws.
EnemyAI13::ChildDeadResult EnemyAI13::ChildDead(bool hasSecondBoom) {
    ChildDeadResult result{};
    if (m_BoomLight) { // 0xAD != 0 (line 680476)
        return result; // already detonated: no-op.
    }
    m_BoomLight = true;          // 0xAD = 1 (line 680479)
    result.latched = true;
    result.detonateBoom = true;  // owner: BulletBoom.StartBoom(boom) (line 680484)
    if (hasSecondBoom) {         // op_Implicit(secondBoom) == 1 (line 680490)
        // owner: BulletBoom.StartBoom(secondBoom) (line 680498).
        result.detonateSecondBoom = true;
    }
    return result;
}

// FAITHFUL: EnemyAI13__FixedUpdate @ game_full.c:680238.
// awake (0x18, line 680267) gates the whole body: not awake -> no-op (NO decay).
// When awake, split on inertial_vel (0x44) <= 1.0 (line 680268). In the
// not-knockback branch a dead (0x38) enemy (line 680269) sleeps (awake = 0, line
// 680270), zeroes velocity and returns; otherwise it steers. In the knockback
// branch (inertial_vel > 1.0, line 680309) the force term is composed and
// inertial_vel decays by friction (0x50): *= friction (line 680339). Pure
// scalar/branch only; rigidbody velocity writes are owner.
EnemyAI13::StepResult EnemyAI13::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 680267): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // inertial_vel <= 1.0 (line 680268)
        if (m_Dead) { // dead (0x38) != 0 (line 680269)
            m_Awake = false; // awake = 0 (line 680270); owner zeroes velocity.
            return StepResult::DeadStop; // returns: steering below does not run.
        }
        // owner: steering velocity composition (lines 680283-680307). NO decay.
        return StepResult::Steer;
    }
    // knockback branch (inertial_vel > 1.0): owner composes steering + force
    // (lines 680310-680338), then inertial_vel decays by friction (line 680339).
    m_InertialVel *= friction;
    return StepResult::Knockback;
}

} // namespace Game
