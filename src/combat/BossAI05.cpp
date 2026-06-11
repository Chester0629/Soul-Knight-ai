#include "combat/BossAI05.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: BossAI05__Scout @ game_full.c:439938.
// Gate: dead(0x38) == 0 && dizzy(0xA1) == 0. The decomp clears target_obj
// (0x7C) and re-runs the chase via get_transform (owner). No RNG draw.
bool BossAI05::Scout() {
    if (m_Dead || m_Dizzy) {
        return false;
    }
    m_HasTarget = false; // target_obj (0x7C) = null
    return true;         // owner: get_transform / re-scout
}

// FAITHFUL: BossAI05__RunReflection @ game_full.c:439963.
// Two Range(-1f, 1f) draws (float, max INCLUSIVE), x then y, normalised into
// move_direction; owner does Animator.SetBool. Mirrors FUN_00fa1e04 (Vector2
// .normalized): a zero vector stays zero.
glm::vec2 BossAI05::WanderDirection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax);
    const float ry = m_Rng.Range(kWanderMin, kWanderMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    return len > 0.0F ? glm::vec2(rx / len, ry / len)
                      : glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI05__ShootReflection @ game_full.c:440021.
// Gate order: can_shoot(0x40) != 0, THEN dead(0x38) == 0 && dizzy(0xA1) == 0.
// Only then does the decomp draw Range(0, 100) (fed to the unrecovered
// StartAtkNN jumptable at 0x547360). Gated-out paths draw nothing (lockstep).
bool BossAI05::ShootReflection(int &rolledOut) {
    if (!m_CanShoot) {
        return false; // can_shoot == 0: no draw, indirect tail-call only
    }
    if (m_Dead || m_Dizzy) {
        return false; // dead or dizzy: no draw
    }
    rolledOut = m_Rng.Range(0, kRollCeiling); // attack-selection roll
    // owner: jumptable dispatch to StartAtkNN (could not recover; head only).
    return true;
}

// FAITHFUL: BossAI05__InAtk01 @ game_full.c:440357.
// Single Range(0, 100); the body is a tail-call into the roll + bullet-spawn
// jumptable and writes no brain field.
int BossAI05::InAtk01Roll() {
    return m_Rng.Range(0, kRollCeiling);
}

// FAITHFUL: BossAI05__StartAtk03 @ game_full.c:440104.
// Only brain write: weapon_lock_target(0x1C) = 0. (move_direction = zero,
// Animator.SetTrigger and RGMusicManager.PlayEffect are owner.) No RNG.
void BossAI05::StartAtk03() {
    m_WeaponLockTarget = false;
}

// FAITHFUL: BossAI05__GetHurt @ game_full.c:440228.
// Gate: awake(0x18) && dead(0x38) == 0 && invisible(0xD0) == 0. On a real hit,
// hp/max_hp < 0.5 (role_attribute +0x1C / +0x18) && angry(0xB0) == 0 ->
// BossAngry. RGEController.GetHurt and BossInfo.UpDateBossHp are owner-side.
bool BossAI05::OnHurt(int hpAfter, int maxHp) {
    if (!m_Awake || m_Dead || m_Invisible) {
        return false; // damage gate failed: GetHurt returns with no effect
    }
    if (maxHp <= 0) {
        return true; // hit accepted; guard div-by-zero (no angry transition)
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction && !m_Angry) {
        BossAngry();
    }
    return true;
}

// FAITHFUL: BossAI05__BossAngry @ game_full.c:440277.
// angry(0xB0) = 1; shoot_cd(0x3C) *= 0.5. (anim.set_speed(1.2f) and
// anim.SetBool("angry", true) are owner.)
void BossAI05::BossAngry() {
    m_Angry = true;                  // 0xB0 = 1
    m_ShootCd *= kAngryShootCdScale; // 0x3C *= 0.5 (faster cadence in phase 2)
    // owner: anim.set_speed(1.2f); anim.SetBool("angry", true).
}

// FAITHFUL: BossAI05__Dizzy @ game_full.c:440574.
// Gate: dead(0x38) == 0 -> dizzy(0xA1) = 1 (anim.SetBool to leave move state is
// owner). The float arg is the stun duration consumed owner-side. No RNG.
bool BossAI05::Dizzy(float /*value1*/) {
    if (m_Dead) {
        return false;
    }
    m_Dizzy = true; // 0xA1 = 1
    return true;
}

// FAITHFUL: BossAI05__TurnInvisible @ game_full.c:440515.
// Gate: invisible(0xD0) == 0 -> invisible = 1 (sprite alpha 0.5 and
// Invoke("BackInvisible", 2f) are owner). No RNG.
bool BossAI05::TurnInvisible() {
    if (m_Invisible) {
        return false;
    }
    m_Invisible = true; // 0xD0 = 1
    // owner: body_render.color alpha = 0.5; Invoke("BackInvisible", 2f).
    return true;
}

// FAITHFUL: BossAI05__BackInvisible @ game_full.c:440548.
// invisible(0xD0) = 0 (sprite alpha back to 1.0 is owner). No gate, no RNG.
void BossAI05::BackInvisible() {
    m_Invisible = false; // 0xD0 = 0
}

// OWNER-side bodies (no brain logic, listed for completeness):
//   BossAI05__Start          @ 439845  - get_transform.
//   BossAI05__FixedUpdate    @ 439869  - FixedUpdateSeed + Rigidbody2D velocity
//                                        / Transform (clears awake byte 0x18 on
//                                        the spawn-anim frame; physics only).
//   BossAI05__StartAtk01     @ 440050  - move_direction = zero; anim.SetBool.
//   BossAI05__StartAtk02     @ 440077  - move_direction = zero; anim.SetBool.
//   BossAI05__StartAtk04     @ 440149  - music PlayEffect; move_direction zero;
//                                        anim.SetBool.
//   BossAI05__FixedRotation  @ 440193  - Transform.get_position (aim).
//   BossAI05__ChildDead      @ 440303  - Singleton<PlayerSaveData>.
//   BossAI05__CreateTransferGate @ 440323 - BossInfo.HideHpBar; Instantiate gate.
//   BossAI05__InAtk02        @ 440374  - music PlayEffect; PrefabPool spawn.
//   BossAI05__InAtk03        @ 440427  - music; Rigidbody2D velocity zero;
//                                        Instantiate RGWeapon/RGELaser.
//   BossAI05__InAtk04        @ 440487  - PrefabManager.GetPrefab; Instantiate.

} // namespace Game
