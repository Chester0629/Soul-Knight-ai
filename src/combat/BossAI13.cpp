#include "combat/BossAI13.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: BossAI13__FixedUpdate @ game_full.c:957902.
// Owner-side: RGEController::FixedUpdateSeed(), then Rigidbody2D.velocity =
// move_direction * role_attribute.speed * (speed_field(0x14)+1.0), and the
// EnemyUpdate vtable tail-call (0x11c). The only brain-visible field write is
// the shooting-brake branch: gates awake(0x18) && !shooting(0x80), and when
// dead(0x38) it zeroes velocity (owner) and clears awake(0x18)=0.
bool BossAI13::FixedUpdateBrake() {
    if (!m_Awake) {
        return false; // gate: awake(0x18)
    }
    if (m_Shooting) {
        return false; // gate: !shooting(0x80)
    }
    if (m_Dead) {
        m_Awake = false; // *(undefined1 *)(param_1 + 6) = 0  -> awake(0x18) = 0
        // owner: Rigidbody2D.set_velocity(0,0); get_transform.
        return true;
    }
    // owner: velocity = move_direction * speed(0x10) * (speed_field(0x14)+1.0).
    return false;
}

// FAITHFUL: BossAI13__Scout @ game_full.c:957973. Gate: !dead(0x38) && !dizzy(0xa1).
// When the gate passes: target_obj(0x7c) = null (then get_transform, owner-side).
// No RNG draw.
bool BossAI13::Scout() {
    if (m_Dead) {
        return false; // gate: dead(0x38)
    }
    if (m_Dizzy) {
        return false; // gate: dizzy(0xa1)
    }
    m_HasTarget = false; // *(undefined4 *)(param_1 + 0x7c) = 0
    return true;
}

// FAITHFUL: BossAI13__RunReflection @ game_full.c:957998.
// Two float draws Range(-1f, 1f) (0xbf800000, 0x3f800000) in x-then-y order,
// assembled via Vector2 ctor (FUN_00fa16ec), normalized (FUN_00fa1e04), and set
// as move_direction (animator "walk"=true is owner-side). Ungated: this method
// is itself the Invoke/reflection target.
glm::vec2 BossAI13::RunReflection() {
    const float rx = m_Rng.Range(kReflectComponentMin, kReflectComponentMax);
    const float ry = m_Rng.Range(kReflectComponentMin, kReflectComponentMax);
    const float len = std::sqrt(rx * rx + ry * ry); // FUN_00fa1e04: normalized
    // owner: RGEController.set_move_direction(dir); Animator.SetBool("walk", true).
    return len > 0.0F ? glm::vec2(rx / len, ry / len)
                      : glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI13__ShootReflection @ game_full.c:958056.
// Gate order: can_shoot(0x40) -> !dead(0x38) -> !dizzy(0xa1); only inside that
// gate (and only if rg_random(0xc) != null, always true once seeded) is the
// single Range(0, 100) draw taken. The roll->StartAtkNN dispatch jumptable was
// NOT recovered (indirect jump 0x00adb738) -> owner-side; the lone draw keeps
// the stream in lockstep regardless of bucketing.
bool BossAI13::ShootReflection(int &outRoll) {
    if (!m_CanShoot) {
        return false; // gate: can_shoot(0x40)  -> NO draw
    }
    if (m_Dead) {
        return false; // gate: dead(0x38)       -> NO draw
    }
    if (m_Dizzy) {
        return false; // gate: dizzy(0xa1)       -> NO draw
    }
    // rg_random != null check (param_1[3]); always seeded in our brain.
    outRoll = m_Rng.Range(0, kRollCeiling);
    // owner: vtable tail-call (0x10c) dispatches to a StartAtkNN.
    return true;
}

// FAITHFUL: BossAI13__GetHurt @ game_full.c:958176 -> BossAI13__BossAngry
// @ game_full.c:958218. GetHurt gate: awake(0x18) && !dead(0x38). The angry test
// is (hp / max_hp < 0.5) && !angry(0xec); the decomp divides
// role_attribute.field(0x1c) / role_attribute.field(0x18), which the sibling
// recreation (BossAI01.cs) documents as hp / max_hp. We take the semantic
// hp/max_hp inputs. BossAngry writes angry(0xec)=1 and shoot_cd(0x3c) *= 0.5
// (animator speed 1.2 and "angry" bool are owner-side).
void BossAI13::OnHurt(int hpAfter, int maxHp) {
    if (!m_Awake) {
        return; // gate: awake(0x18)
    }
    if (m_Dead) {
        return; // gate: dead(0x38)
    }
    // base RGEController::GetHurt applies the damage (owner).
    if (m_Angry || maxHp <= 0) {
        return; // !angry(0xec) gate; guard divide-by-zero
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction) {
        // BossAngry(): the two real field writes.
        m_Angry = true;                      // *(undefined1 *)(param_1 + 0xec) = 1
        m_ShootCd *= kAngryShootCdScale;     // shoot_cd(0x3c) *= 0.5
        // owner: Animator.set_speed(1.2); Animator.SetBool("angry", true).
    }
    // owner: BossInfo.UpDateBossHp(hp, max_hp).
}

// FAITHFUL: BossAI13__Dizzy @ game_full.c:958520. Gate: !dead(0x38). When the
// gate passes, writes dizzy(0xa1)=1 (Animator.SetBool("walk", false) is
// owner-side). No RNG draw.
bool BossAI13::OnDizzy() {
    if (m_Dead) {
        return false; // gate: dead(0x38)
    }
    m_Dizzy = true; // *(undefined1 *)(param_1 + 0xa1) = 1
    return true;
}

// FAITHFUL: BossAI13__OnGameStateChange @ game_full.c:958299. When
// game_state==1 and the maker's room reports ready (offset chain
// 0x25->0x28->0x10 == 1), writes awake(0x18)=1 then vtable-dispatches
// StartBossAI (jumptable 0x00adc9f8 not recovered -> owner-side). No RNG.
bool BossAI13::OnGameStateChange(int gameState, bool roomReady) {
    if (gameState != 1) {
        return false;
    }
    if (!roomReady) {
        return false; // the_room.field(0x10) != 1
    }
    m_Awake = true; // *(undefined1 *)(param_1 + 6) = 1  -> awake(0x18) = 1
    // owner: vtable StartBossAI (0x104).
    return true;
}

} // namespace Game
