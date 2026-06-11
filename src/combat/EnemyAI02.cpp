#include "combat/EnemyAI02.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI02__Scout @ game_full.c:676531.
// Gate (lines 676541-676546): cVar1 = dead (0x38); bVar2 = (dead == 0); if dead
// is 0 then cVar1 = dizzy (0xA1); proceed only when (dead == 0 && dizzy == 0).
// When active: target_obj = null (0x7C, line 676547), then ONLY when rg_random
// (0x0C) != 0 (line 676548) draw rg_random.Range(0, 10) (line 676550). The
// re-detect/re-target tail (FUN_010b7dcc, line 676552) is tail-call-truncated
// and not recoverable -> not modelled.
int EnemyAI02::Scout() {
    if (m_Dead || m_Dizzy) {
        return -1; // gated: no draw taken (keeps stream lockstep with original)
    }
    m_HasTarget = false; // target_obj = null (0x7C, line 676547)
    if (!m_HasRng) {
        return -1; // rg_random == 0 (line 676548): no draw taken.
    }
    return m_Rng.Range(0, kScoutRerollCeiling); // line 676550 (max EXCLUSIVE)
}

// FAITHFUL: EnemyAI02__RunReflection @ game_full.c:676560.
// Two rg_random.Range(-1f, 1f) draws (lines 676599, 676604), Vector2 built
// (FUN_00fa16ec, line 676605) then normalized (FUN_00fa1e04, line 676606),
// stored as move_direction (set_move_direction, line 676607). No gate.
// owner: anim.SetBool("walk", true) at the tail (line 676613). The preceding
// target_obj position read (lines 676586-676593) is owner facing only.
glm::vec2 EnemyAI02::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 676599 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 676604 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI02__ShootReflection @ game_full.c:676618.
// Gate (lines 676629-676634): same dead (0x38) || dizzy (0xA1) gate as Scout.
// When active the only pure state write is role_attribute.speed_rate (0x70+0x14)
// -= 0.5 (line 676646). No shooting/can_shoot/weapon_lock_target writes here.
// owner: RGEHand.SetAttackTrigger(hand@0x6C) (line 676639) and
// Invoke("ShootReflection"-style, shoot_cd@0x3C) (line 676640). No RNG draws.
bool EnemyAI02::ShootReflection(float &speedRate) {
    if (m_Dead || m_Dizzy) {
        return false; // gated: speedRate untouched.
    }
    speedRate -= kShootSpeedRatePenalty; // role_attribute speed_rate (0x70+0x14) -= 0.5
    return true;
}

// FAITHFUL: EnemyAI02__FixedUpdate @ game_full.c:676409.
// Gated on awake (0x18, line 676438): not awake -> complete no-op (no decay, no
// velocity write, no awake clear). When awake the inertial_vel (0x44) split:
//   inertial_vel <= 1.0 (line 676439): steer branch. dead (0x38, line 676440)
//     clears awake (awake = 0, line 676441) + zeroes velocity (DeadStop);
//     otherwise normal steering (Steer). NO friction decay in either case.
//   inertial_vel > 1.0 (else, line 676480): steer + knockback force composed,
//     then inertial_vel *= friction (0x50, line 676510) (Knockback).
// No early return; live paths converge on the can_shoot (0x40) facing check
// (line 676512), owner-only. Pure scalar/branch only; rigidbody writes are owner.
EnemyAI02::StepResult EnemyAI02::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 676438): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // inertial_vel <= 1.0 (line 676439)
        if (m_Dead) {                 // dead (0x38, line 676440)
            m_Awake = false;          // awake = 0 (line 676441); owner zeroes velocity.
            return StepResult::DeadStop;
        }
        return StepResult::Steer;     // normal steering (676454-676478), no decay.
    }
    // inertial_vel > 1.0 (else branch, line 676480): knockback compose then decay.
    m_InertialVel *= friction;        // inertial_vel *= friction (0x50, line 676510)
    return StepResult::Knockback;
}

// FAITHFUL: EnemyAI02__OnGameStateChange @ game_full.c:676697.
// Wake gate: when param_2 (gameState) == 1, piVar1 = temp_enemy (0x19); the body
// returns early unless (param_2 == 1 && temp_enemy == 0) (lines 676702-676705).
// Then the_maker (param_1[0x25] = 0x94) must be non-null (line 676706); its
// the_room (+0x28) must be non-null (line 676709); and the_room.state (+0x10) == 1
// (line 676712). On that path the only pure state write is awake (0x18) = 1 (the
// byte `param_1 + 6`, line 676713). The two trailing virtual calls (vtable +0x11c
// and +0x104, lines 676714-676717) are jumptable-truncated indirect tail-calls
// (StartEnemyAI-style dispatch) and are owner concern -- NOT modelled here.
bool EnemyAI02::OnGameStateChange(int gameState) {
    if (gameState != 1 || m_TempEnemy) {
        return false; // gated: only gameState==1 && temp_enemy==0 proceeds.
    }
    // the_maker(0x94)!=null && the_room(0x28)!=null && the_room.state(0x10)==1.
    // The owner mirrors that whole chain into m_RoomReady.
    if (!m_RoomReady) {
        return false; // room not yet ready -> no wake (state != 1 or chain null).
    }
    m_Awake = true; // awake (0x18) = 1 (the byte param_1 + 6, line 676713).
    // TODO[verify]: virtual dispatch tail (vtable +0x11c, +0x104) -- owner.
    return true;
}

} // namespace Game
