#include "combat/EnemyAIShark.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAIShark__Scout @ game_full.c:681448 (+ tail FUN_007ff020 @
// 681498 / FUN_007ff0e0 @ 681558).
// Gate (lines 681458-681463): bVar2 = (dizzy(0xA1) == 0); if bVar2 then
// cVar1 = dead(0x38); body runs only when (bVar2 && cVar1 == 0), i.e.
// !dizzy && !dead. When active: target_obj = null (0x7C, line 681464) then the
// tail-called continuation does CircleCastAll (owner) and the single
// rg_random.Range(0, 10) draw (line 681553/681595). The cast hit -> min_distance
// (0x88) = 100.0 branch and the cast itself are owner/truncated -> not modelled;
// only the gate, target clear, and the draw are pure.
int EnemyAIShark::Scout() {
    if (m_Dizzy || m_Dead) {
        return -1; // gated: no draw taken (keeps stream lockstep with original)
    }
    m_HasTarget = false; // target_obj = null (0x7C, line 681464)
    // owner (continuation): CircleCastAll detect; on hit min_distance = 100.0.
    return m_Rng.Range(0, kScoutRerollCeiling); // line 681553/681595 (max EXCL)
}

// FAITHFUL: EnemyAIShark__RunReflection @ game_full.c:681600.
// Ungated (no dead/dizzy guard in the decomp). Reads target_obj position when
// non-null (lines 681626-681633, owner facing only). Two rg_random.Range(-1f, 1f)
// draws (lines 681639, 681644), Vector2 normalized (FUN_00fa16ec build +
// FUN_00fa1e04 normalize), stored as move_direction (set_move_direction, 681647).
// owner: anim.SetBool(StringLiteral_6550, true) at the tail (line 681653).
glm::vec2 EnemyAIShark::RunReflection() {
    const float rx = m_Rng.Range(kDirMin, kDirMax); // line 681639 (max INCL)
    const float ry = m_Rng.Range(kDirMin, kDirMax); // line 681644 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAIShark__ShootReflection @ game_full.c:681658.
// Gate (lines 681668-681673): bVar2 = (dead(0x38) == 0); if bVar2 then
// cVar1 = dizzy(0xA1); body runs only when (bVar2 && cVar1 == 0), i.e.
// !dead && !dizzy. When active: rg_random.Range(0, 100) (line 681676). The
// post-draw body is tail-call-truncated (FUN_010b7dcc, line 681679) -> not
// modelled. Returns the roll; the draw keeps the stream lockstep.
int EnemyAIShark::ShootReflection() {
    if (m_Dead || m_Dizzy) {
        return -1; // gated: no draw taken
    }
    return m_Rng.Range(0, kShootRollCeiling); // line 681676 (max EXCL)
}

// FAITHFUL: EnemyAIShark__Dash @ game_full.c:681686.
// can_shoot = false (0x40 = 0, line 681693). No RNG.
// owner: Invoke(StringLiteral_6552, shoot_cd@0x3C) schedules the dash coroutine
// (line 681694); anim.SetBool(StringLiteral_6975, true) (line 681700).
void EnemyAIShark::Dash(float &outDashDelay, float shootCd) {
    m_CanShoot = false;       // 0x40 = 0 (line 681693)
    outDashDelay = shootCd;   // 0x3C delay for Invoke("Dashing") (line 681694)
}

// FAITHFUL: EnemyAIShark_<Dashing>c__Iterator0__MoveNext @ game_full.c:681870.
// state(+0x1C): 0 -> init branch (timer +8 = 0, interval +0xC = 0.2; lines
// 681896-681897); 1 -> advance branch (timer += interval; line 681890). After
// the timer update: while the self object exists (op_Implicit, line 681904) AND
// timer < 1.5 (line 681905) -> emit Wave (line 681907) and continue; else end.
// We mirror the existence check as "not dead".
bool EnemyAIShark::DashingStep() {
    if (!m_DashingStarted) {
        m_WaveTimer = 0.0F;             // iterator +8 = 0 (line 681896)
        m_WaveInterval = kWaveInterval; // iterator +0xC = 0.2 (line 681897)
        m_DashingStarted = true;
    } else {
        m_WaveTimer += m_WaveInterval;  // timer += interval (line 681890)
    }
    // op_Implicit(self) && timer < 1.5 -> Wave + continue (lines 681904-681912).
    if (!m_Dead && m_WaveTimer < kWaveTimerLimit) {
        // owner: EnemyAIShark__Wave (Instantiate(wave_bullet)) (line 681907),
        // then yield WaitForSeconds (line 681909).
        return true;
    }
    m_DashingStarted = false; // coroutine terminates; ready for the next dash.
    return false;
}

// FAITHFUL: EnemyAIShark__GetForce @ game_full.c:681811.
// Base RGEController__GetForce applied ONLY when not dashing (0xB1 == 0, line
// 681814). Base stores force_direction and clamps the magnitude to 28 into
// inertial_vel. While dashing the impulse is ignored entirely.
float EnemyAIShark::GetForce(float value) {
    if (m_Dashing) {
        return m_InertialVel; // 0xB1 != 0: base GetForce skipped; impulse ignored.
    }
    float v = value;
    if (v > kForceCap) { // base hard cap of 28 (RGEController__GetForce)
        v = kForceCap;
    }
    m_InertialVel = v; // 0x44
    return m_InertialVel;
}

// FAITHFUL: EnemyAIShark__FixedUpdate @ game_full.c:681339.
// Whole body gated on awake (0x18, line 681368): not awake -> no-op. When awake:
//   inertial_vel(0x44) <= 1.0 (line 681369):
//     dead(0x38) (line 681370) -> awake = 0 (line 681371) + zero velocity
//                                 (DeadClear); NO decay.
//     not dead                 -> normal steering (Steer); NO decay.
//   inertial_vel > 1.0 (line 681410) -> knockback impulse added (681431-681439)
//                                 + inertial_vel *= friction (0x50, line 681440)
//                                 (Knockback).
// Pure scalar/branch only; rigidbody velocity writes are owner.
EnemyAIShark::StepResult EnemyAIShark::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 681368): nothing happens.
    }
    if (m_InertialVel <= kKnockbackThreshold) { // inertial_vel <= 1.0 (line 681369)
        if (m_Dead) {                            // dead (line 681370)
            m_Awake = false; // awake = 0 (line 681371); zero velocity is owner.
            return StepResult::DeadClear; // no decay on this path.
        }
        return StepResult::Steer; // normal steering (681384-681408); no decay.
    }
    // inertial_vel > 1.0 (line 681410): knockback added then decayed by friction.
    m_InertialVel *= friction; // inertial_vel *= friction (0x50, line 681440)
    return StepResult::Knockback;
}

} // namespace Game
