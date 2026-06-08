#include "combat/EnemyAI01.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: EnemyAI01__Scout @ game_full.c:676136.
// Gate (lines 676146-676151): skip while dead (0x38) or dizzy (0xA1). When
// active: target_obj = null (0x7C, line 676152) then, if rg_random (0xC) != 0,
// rg_random.Range(0, 10) (line 676155). The re-detect/re-target tail
// (FUN_010b7dcc, line 676158) is tail-call-truncated and not modelled.
int EnemyAI01::Scout() {
    if (m_Dead || m_Dizzy) {
        return -1; // gated: no draw taken (keeps stream lockstep with original)
    }
    m_HasTarget = false; // target_obj = null (0x7C)
    return m_Rng.Range(0, kScoutRerollCeiling); // line 676155 (max EXCLUSIVE)
}

// FAITHFUL: EnemyAI01__RunReflection @ game_full.c:676254.
// Two rg_random.Range(-1f, 1f) draws (lines 676293, 676298), Vector2 normalized
// (FUN_00fa16ec build + FUN_00fa1e04 normalize, lines 676299-676300), stored as
// move_direction (set_move_direction, line 676301).
// owner: target_obj position read (676280-676287, facing only) + anim.SetBool
// (walk, true) at the tail (line 676307).
glm::vec2 EnemyAI01::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax); // line 676293 (max INCL)
    const float ry = m_Rng.Range(kWanderMin, kWanderMax); // line 676298 (max INCL)
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection = len > 0.0F ? glm::vec2(rx / len, ry / len)
                                 : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: EnemyAI01__ShootReflection @ game_full.c:676312.
// Gate (lines 676322-676327): skip while dead (0x38) or dizzy (0xA1). When
// active: can_shoot = false (0x40, line 676328). NOTE: this decomp does NOT set
// shooting (0x80) -- only can_shoot is written. No RNG draws.
bool EnemyAI01::ShootReflection(float &outShootCd, float shootCd) {
    if (m_Dead || m_Dizzy) {
        return false;
    }
    m_CanShoot = false; // 0x40 = 0 (line 676328)
    // owner: RGEHand.SetAttackTrigger(hand@0x6C) (line 676330),
    // Invoke("ShootReflection", shoot_cd@0x3C) re-fire (line 676331),
    // then indirect tail call (jumptable truncated at 0x007e723c, line 676334).
    outShootCd = shootCd; // 0x3C delay -> ShootReflection re-fire
    return true;
}

// FAITHFUL: EnemyAI01__FixedRotation @ game_full.c:676345.
// Builds a zero vector (FUN_00fa16ec, line 676359, owner), then branches on
// target_obj (0x7C) != null (line 676365). Both branches tail-call owner
// Transform reads: non-null -> get_position(target_obj) (line 676375);
// null -> get_transform(self) (line 676368). No pure state is written.
EnemyAI01::RotationResult EnemyAI01::FixedRotation() const {
    return m_HasTarget ? RotationResult::AimAtTarget // get_position(target_obj)
                       : RotationResult::FaceDefault; // get_transform(self)
}

// FAITHFUL: EnemyAI01__OnGameStateChange @ game_full.c:676380.
// Returns immediately unless game_state == 1 (line 676385). When the maker's
// room is ready (the_maker(0x94).the_room(0x28).field(0x10) == 1, lines
// 676388-676397), latches awake = true (0x18, line 676398) and tail-calls the
// owner's wake dispatch (indirect call, line 676401, not modelled).
bool EnemyAI01::OnGameStateChange(int gameState, bool roomReady) {
    if (gameState != kRunningGameState) { // line 676385
        return false;
    }
    if (!roomReady) { // the_maker.the_room.field(0x10) == 1 (line 676397)
        return false;
    }
    const bool wasAwake = m_Awake;
    m_Awake = true; // 0x18 = 1 (line 676398)
    return !wasAwake; // true only on the awake transition
}

// FAITHFUL: EnemyAI01__FixedUpdate @ game_full.c:675946.
// The whole body is gated on awake (0x18, line 675975): not awake -> no-op (NO
// friction decay, NO velocity write, awake unchanged). When awake, the path
// splits on (inertial_vel <= 1.0 OR kinematic) (line 675976):
//   - not-knockback path: if dead (0x38, line 675977) -> awake = 0 (line 675978),
//     velocity zeroed (owner); NO decay -> Dead. Otherwise normal steering (block
//     675991-676016); NO decay -> Steer.
//   - knockback path (inertial_vel > 1.0 AND not kinematic): else branch
//     (676018-676050) composes force into velocity and decays inertial_vel *=
//     friction (0x50, line 676049) -> Knockback.
// Pure scalar/branch only; rigidbody velocity writes are owner.
EnemyAI01::StepResult EnemyAI01::FixedUpdateStep(float friction) {
    if (!m_Awake) {
        return StepResult::Asleep; // awake gate (line 675975): nothing happens.
    }
    const bool notKnockback =
        (m_InertialVel <= kKnockbackThreshold) || m_Kinematic; // line 675976
    if (notKnockback) {
        if (m_Dead) { // line 675977
            m_Awake = false; // 0x18 = 0 (line 675978); velocity zeroed (owner).
            return StepResult::Dead; // NO decay on this path.
        }
        return StepResult::Steer; // steering block (675991-676016); NO decay.
    }
    // else branch: inertial_vel > 1.0 AND not kinematic (676018-676050).
    m_InertialVel *= friction; // line 676049
    return StepResult::Knockback;
}

} // namespace Game
