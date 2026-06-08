#include "combat/BossAI12.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: BossAI12__Scout @ game_full.c:957229.
// Gate: dizzy(0xa1) must be clear AND, only then, dead(0x38) must be clear;
// when both clear, target_obj(0x7c) = 0 (the trailing get_transform is owner-
// side). The decomp short-circuits: it reads 0x38 only when 0xa1 == 0. No draw.
void BossAI12::Scout() {
    const bool notDizzy = !m_Dizzy;
    const bool notDead = notDizzy && !m_Dead;
    if (notDizzy && notDead) {
        m_HasTarget = false; // target_obj(0x7c) = 0
        // owner: get_transform(this) (no further brain effect).
    }
}

// FAITHFUL: BossAI12__RunReflection @ game_full.c:957254.
// Two Range(-1f, 1f) float draws (x at :957289, y at :957295 in stream order),
// normalised; owner writes set_move_direction + anim.SetBool("walk", true).
glm::vec2 BossAI12::WanderDirection() {
    const float rx = m_Rng.Range(kWanderAxisMin, kWanderAxisMax);
    const float ry = m_Rng.Range(kWanderAxisMin, kWanderAxisMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    return len > 0.0F ? glm::vec2(rx / len, ry / len)
                      : glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI12__ShootReflection @ game_full.c:957312.
// param_1 is int*, so: param_1[0x10] = byte 0x40 = can_shoot, param_1[0xe] =
// byte 0x38 = dead, (int)param_1 + 0xa1 = dizzy, param_1[3] = byte 0xc = rng.
// Draw gate (all must hold): can_shoot(0x40) != 0, dead(0x38) == 0,
// dizzy(0xa1) == 0. The decomp checks 0x38 first; only if 0x38 == 0 does it read
// 0xa1 (short-circuit bVar2). The single Range(0, 100) draw fires inside that
// nest; any gated-out path takes NO draw (stream lockstep). The roll then drives
// an indirect/vtable tail call (*(*param_1 + 0x10c)) - the roll->InAtkNN jumptable
// is NOT recoverable, so we return the raw roll for the caller to dispatch.
bool BossAI12::ShootReflectionRoll(bool canShoot, bool dead, bool dizzy,
                                   int &outRoll) {
    if (!canShoot) {
        return false;
    }
    if (!dead && !dizzy) {
        outRoll = m_Rng.Range(0, kRollCeiling); // Range(0, 100), max exclusive
        return true;
    }
    return false;
}

// FAITHFUL: BossAI12__GetHurt @ game_full.c:957378 -> BossAI12__BossAngry @ 957423.
// Gate: ignored unless awake(0x18) && !dead(0x38). Angry trigger:
// role_attribute(0x70): max_hp(+0x18), hp(+0x1c); hp/max_hp < 0.5 AND angry(0xcc)
// not yet set. BossAngry sets angry(0xcc)=1 and shoot_cd(0x3c) *= 0.5 (the
// animator set_speed(1.2f) + SetBool("angry") are owner-side). No RNG draw.
void BossAI12::OnHurt(bool awake, int hpAfter, int maxHp) {
    if (!awake || m_Dead) {
        return; // GetHurt early-out: !awake || dead
    }
    // RGEController.GetHurt applies the damage (owner-side hp write) first.
    if (maxHp <= 0) {
        return; // guard the hp/max_hp division (decomp divides unconditionally)
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction && !m_Angry) {
        // BossAngry():
        m_Angry = true;                  // angry(0xcc) = 1
        m_ShootCd *= kAngryShootCdScale; // shoot_cd(0x3c) *= 0.5
        // owner: anim.set_speed(1.2f), anim.SetBool("angry", true).
    }
    // owner: if ai_parent(0xac) != null -> BossAI12Parent.UpDateBossHp (HP UI).
}

// FAITHFUL: BossAI12__Dizzy @ game_full.c:957800.
// Gate: dead(0x38) == 0; only then dizzy(0xa1) = 1
// (anim.SetBool("walk", false) is owner-side). No RNG draw.
void BossAI12::OnDizzy() {
    if (!m_Dead) {
        m_Dizzy = true; // dizzy(0xa1) = 1
        // owner: anim.SetBool("walk", false).
    }
}

// FAITHFUL: BossAI12__InAtk01 @ game_full.c:957587.
// Reads angry(0xcc) (param_1[0x33]) to pick the instantiate count: 1 calm,
// 2 angry. The decomp's (-n <= n) loop guard is always true. No RNG draw;
// RGWeapon Instantiate + RGMusicManager.PlayEffect are owner-side.
int BossAI12::InAtk01BulletCount() const {
    return m_Angry ? kInAtk01BulletsAngry : kInAtk01BulletsCalm;
}

// FAITHFUL: BossAI12__InAtk03 @ game_full.c:957718.
// __udivsi3(0xb4, divisor) with divisor = angry(0xcc) ? 6 : 5, i.e. the bullet
// fan's per-shot angle = 180 / divisor degrees. No RNG draw (Instantiate is
// owner-side).
int BossAI12::InAtk03AngleStep() const {
    const int divisor = m_Angry ? kInAtk03DivisorAngry : kInAtk03DivisorCalm;
    return kInAtk03Arc / divisor;
}

// FAITHFUL: BossAI12__EndAtk04 @ game_full.c:957781.
// param[0x1c]->field 0x14 += -1.0 (relax the attack-param shoot_cd by 1s), then
// an indirect tail call (jumptable not recovered) handled by the owner. No draw.
float BossAI12::EndAtk04ShootCd(float current) const {
    return current + kEndAtk04ShootCdDelta;
}

} // namespace Game
