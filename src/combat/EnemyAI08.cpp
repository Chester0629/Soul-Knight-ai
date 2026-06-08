#include "combat/EnemyAI08.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI08__Scout @ game_full.c:678230.
// Gate (lines 678240-678245): the decomp reads dizzy (0xA1) first; only when not
// dizzy does it read dead (0x38) (short-circuit). The active branch requires BOTH
// not-dizzy AND not-dead. When active: target_obj = null (0x7C, line 678246) then
// rg_random.Range(0, 10) (line 678249). The re-detect/re-target tail
// (FUN_010b7dcc, line 678252) is tail-call-truncated and not recoverable -> not
// modelled.
int EnemyAI08::Scout() {
    if (m_Dizzy || m_Dead) {
        return -1; // gated: no draw taken (keeps stream lockstep with original)
    }
    m_HasTarget = false; // target_obj = null (0x7C, line 678246)
    return m_Rng.Range(0, kScoutRerollCeiling); // line 678249 (max EXCLUSIVE)
}

// FAITHFUL: EnemyAI08__RunReflection @ game_full.c:678259.
// Two rg_random.Range(-1f, 1f) draws (lines 678298, 678303), Vector2 normalized
// (FUN_00fa16ec build + FUN_00fa1e04 normalize, lines 678304-678305), stored as
// move_direction (set_move_direction, line 678306).
// The leading target_obj position read (lines 678285-678292) feeds only owner
// facing; it takes no RNG draw and is not modelled.
// owner: anim.SetBool("walk", true) at the tail (line 678312).
glm::vec2 EnemyAI08::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 678298 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 678303 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI08__GetWeapon @ game_full.c:678369.
// The whole recoverable body is one rg_random.Range(0, total) draw (line 678381),
// total being the 0xB4 weapon-count field. The tail (use the index) is
// tail-call-truncated and is owner work. max EXCLUSIVE.
int EnemyAI08::GetWeapon(int total) {
    return m_Rng.Range(0, total); // line 678381 (max EXCLUSIVE)
}

// FAITHFUL: EnemyAI08__DisAppear @ game_full.c:678483.
// awake (0x18) = 0 (line 678490), dead (0x38) = 1 (line 678491). The tail
// (get_transform -> drop item / despawn VFX, line 678493) is tail-call-truncated
// and owner work. No gate, no RNG.
void EnemyAI08::DisAppear() {
    m_Awake = false; // 0x18 = 0 (line 678490)
    m_Dead = true;   // 0x38 = 1 (line 678491)
    // owner: get_transform -> spawn dropped item / play despawn effect (line 678493).
}

// FAITHFUL: EnemyAI08__FixedUpdate @ game_full.c:678121.
// The whole body is gated on awake (0x18, line 678150): not awake -> no-op (NO
// friction decay, NO velocity write). When awake, inertial_vel (0x44) > 1.0 (the
// else branch, line 678151) is the knockback path: the knockback velocity
// replaces steering and inertial_vel is decayed by friction (0x50): *= friction
// (line 678222). When inertial_vel <= 1.0 AND dead (0x38) (line 678152) the dead
// branch sets awake = 0, zeros velocity, and tail-returns (line 678164) so the
// steer write does not run. Otherwise normal steering. Pure scalar/branch only;
// rigidbody velocity writes are owner.
EnemyAI08::StepResult EnemyAI08::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 678150): nothing happens.
    }
    if (m_InertialVel > kKnockbackThreshold) { // else branch: 1.0 < inertial_vel
        // knockback replaces steering: decay (line 678222). The dead/steer block
        // (the inertial_vel <= 1.0 path) does not run.
        m_InertialVel *= friction;
        return StepResult::Knockback;
    }
    if (m_Dead) { // inertial_vel <= 1.0 AND dead (0x38, line 678152)
        m_Awake = false; // 0x18 = 0 (line 678153); owner zeros velocity (678162).
        return StepResult::DeadStop; // dead branch tail-returns (line 678164).
    }
    return StepResult::Steer; // plain steering block (lines 678166-678190).
}

} // namespace Game
