#include "combat/BossAI06.hpp"

#include <cmath>

namespace Game {

// Owner-only bodies (no brain logic; named for traceability against the decomp):
//   BossAI06__Start            @ game_full.c:440595  (get_transform tail).
//   BossAI06__FixedUpdate      @ game_full.c:440608  (FixedUpdateSeed + Rigidbody2D
//                                                     velocity composition, transform).
//   BossAI06__Scout            @ game_full.c:440758  (gated no-op: when !dead && !dizzy
//                                                     clears target_obj(0x7c)=0; movement
//                                                     is owner-side, no RNG draw).
//   BossAI06__FixedRotation    @ game_full.c:440969  (Transform.position aim read).
//   BossAI06__StartAtk01       @ game_full.c:440870  (move_direction=zero, SetBool "walk").
//   BossAI06__StartAtk02       @ game_full.c:440897  (audio + move_direction=zero, SetBool).
//   BossAI06__CreateTransferGate @ game_full.c:441106 (HideHpBar + Instantiate gate VFX).
//   BossAI06__ChildDead        @ game_full.c:441086  (Singleton<PlayerSaveData> read).
//   BossAI06__Atk1/Atk2/Atk3   @ 441142/441194/441224 (audio + Instantiate<RGWeapon>;
//                                                       Atk3 also Invoke chain + tail).
//   BossAI06__ChildsCreateBullet0/1/2, Child1CreateBullet, Child3CreateBullet
//                                                     (iterate ball_childs(0xb4), call
//                                                      BossAI06Child.Atk*, Invoke; no draw).
//   BossAI06__Dizzy            @ game_full.c:441576  (latch dizzy(0xa1)=1 when !dead,
//                                                     owner SetBool "walk" false; no draw).

// FAITHFUL: BossAI06__GetHurt @ game_full.c:441004 -> BossAI06__BossAngry @ 441046.
// Gates: not awake (0x18) or dead (0x38) -> ignore. role_attribute(0x70): the ratio
// is hp(+0x1c)/max_hp(+0x18) < 0.5 with the not-yet-angry guard (0xb0 == 0). The HP
// subtraction / damage number / BossInfo.UpDateBossHp are owner-side (RGEController).
void BossAI06::OnHurt(int hpAfter, int maxHp, bool awake) {
    if (!awake) {
        return; // 0x18 == 0: hit before the boss woke up is ignored.
    }
    if (m_Dead) {
        return; // 0x38 != 0: already dead.
    }
    if (m_Angry || maxHp <= 0) {
        return; // not-yet-angry guard (0xb0) + divide-by-zero guard.
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction) {
        BossAngry();
    }
}

// FAITHFUL: BossAI06__BossAngry @ game_full.c:441046 (field 0xb0 = 1; owner:
// anim.set_speed(1.5f / 0x3fc00000), anim.SetBool("angry", true / StringLiteral_6560)).
void BossAI06::BossAngry() {
    m_Angry = true; // 0xb0 = 1 (latched once via OnHurt's guard).
}

// FAITHFUL: BossAI06__RunReflection @ game_full.c:440783 (two Range(-1f, 1f) float
// draws -> Vector2 -> normalized into move_direction; owner: anim.SetBool "walk", true).
// Draws exactly two floats, in order (x then y), max INCLUSIVE.
glm::vec2 BossAI06::WanderDirection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax);
    const float ry = m_Rng.Range(kWanderMin, kWanderMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    return len > 0.0F ? glm::vec2(rx / len, ry / len)
                      : glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI06__ShootReflection @ game_full.c:440841. The Range(0, 100) roll
// fires only when can_shoot(0x40) != 0 && !dead(0x38) && !dizzy(0xa1) &&
// rg_random(0xc) != null; otherwise the original takes the indirect tail (jumptable
// not recovered) with NO draw. Gated-out path consumes no RNG (stream lockstep). The
// rg_random-null branch is always taken for a seeded brain, so it is not a separate
// gate. roll->StartAtkNN dispatch not recovered; only the single draw is modelled.
bool BossAI06::ShootReflectionRoll(int &outRoll) {
    if (m_CanShoot && !m_Dead && !m_Dizzy) {
        outRoll = m_Rng.Range(0, kRollCeiling);
        return true;
    }
    return false; // gated out: no draw.
}

// FAITHFUL: BossAI06__Atk3CreateBullet @ game_full.c:441559 (single Range(0, 0x168)
// int draw; bullet spawn is owner-side). Draws RNG only, writes no field.
int BossAI06::Atk3CreateBulletAngle() {
    return m_Rng.Range(0, kAtk3AngleCeiling);
}

// FAITHFUL: BossAI06__StartAtk03 @ game_full.c:440941 (field 0xf0 = 1; owner:
// move_direction = Vector2.zero, anim.SetBool("walk", false)). No RNG draw.
void BossAI06::StartAtk03() {
    m_Atk3Shooting = true; // 0xf0 = 1.
}

// FAITHFUL: BossAI06__InAtk02 @ game_full.c:441263 (switch on atk2_value(0xe8)).
// Cases 1/2/4 each draw one Range(0,100); case 3 (audio + child spawn tail) and the
// default draw nothing. atk2_invoke_time(0xec) and atk2_shooting(0xf3) writes per case.
bool BossAI06::InAtk02(int atk2Value, int &outRoll) {
    m_Atk2Value = atk2Value; // cache the owner-driven selector (0xe8) for inspection.
    switch (atk2Value) {
    case 1:
        m_Atk2Shooting = true;            // 0xf3 = 1.
        m_Atk2InvokeTime = kAtk2InvokeTime; // 0xec = +1.0f (0x3f800000).
        outRoll = m_Rng.Range(0, kInAtk02RollCeiling);
        return true;
    case 2:
        m_Atk2InvokeTime = -kAtk2InvokeTime; // 0xec = -1.0f (0xbf800000).
        outRoll = m_Rng.Range(0, kInAtk02RollCeiling);
        return true;
    case 3:
        // Fall-through tail: atk2_shooting(0xf3)=1, audio (PlayEffect), child spawn
        // (ChildsCreateBullet2) and atk2_invoke_time(0xec)=-1.0, then Invoke. The
        // audio/child/Invoke are owner-side; only the field writes are modelled, and
        // this case draws NO RNG.
        m_Atk2Shooting = true;               // 0xf3 = 1.
        m_Atk2InvokeTime = -kAtk2InvokeTime; // 0xec = -1.0f.
        return false;                        // no draw.
    case 4:
        m_Atk2InvokeTime = -kAtk2InvokeTime; // 0xec = -1.0f.
        // Child1CreateBullet + four Invoke()s are owner-side.
        outRoll = m_Rng.Range(0, kInAtk02RollCeiling);
        return true;
    default:
        return false; // no write, no draw.
    }
}

// FAITHFUL: BossAI06__EndAtk02 @ game_full.c:441506 ((int)p+0xf1 = 0, (int)p+0xf3 = 0;
// owner: PlayEffect, reset hand localEulerAngles to 0, anim.SetInteger 0, Invoke,
// indirect tail). No RNG draw.
void BossAI06::EndAtk02() {
    m_BallGroupRotation = false; // 0xf1 = 0.
    m_Atk2Shooting = false;      // 0xf3 = 0.
}

} // namespace Game
