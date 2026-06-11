#include "combat/BossAI09.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: BossAI09__RunReflection @ game_full.c:443219/443225.
// Two Range(-1.0f, 1.0f) draws (immediates 0xbf800000/0x3f800000), composed into
// a Vector2 then normalized into move_direction. The optional target_obj(0x7c)
// position read and the trailing animator SetBool("run", true) are owner-side.
glm::vec2 BossAI09::RunReflection() {
    const float rx = m_Rng.Range(kWanderMin, kWanderMax);
    const float ry = m_Rng.Range(kWanderMin, kWanderMax);
    const float len = std::sqrt(rx * rx + ry * ry);
    m_MoveDirection =
        len > 0.0F ? glm::vec2(rx / len, ry / len) : glm::vec2(0.0F, 0.0F);
    return m_MoveDirection;
}

// FAITHFUL: BossAI09__ShootReflection @ game_full.c:443256.
// Single Range(0, 100) draw GATED by can_shoot(0x40) != 0 && dead(0x38) == 0 &&
// dizzy(0xa1) == 0. Gated-out path takes NO draw (the original branches straight
// to the indirect tail call). The roll->StartAtkNN jumptable was not recovered
// (indirect jump at 0x00557c98, "Too many branches"); the 5 equal buckets are a
// reconstruction. The single draw keeps the stream in lockstep regardless.
int BossAI09::ShootReflection() {
    if (!(m_CanShoot && !m_Dead && !m_Dizzy)) {
        return kNoAttack; // gated out: no RNG draw, stream untouched
    }
    const int roll = m_Rng.Range(0, kRollCeiling);
    int idx = roll / (kRollCeiling / kAttackCount) + 1;
    if (idx > kAttackCount) {
        idx = kAttackCount;
    }
    return idx;
}

// FAITHFUL: BossAI09__StartAtk02 @ game_full.c:443300/443305.
// set_move_direction(Vector2.zero); owner then SetTrigger. No RNG draw.
void BossAI09::StartAtk02() {
    m_MoveDirection = glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI09__StartAtk03 @ game_full.c:443327/443332.
// set_move_direction(Vector2.zero); owner then SetTrigger. No RNG draw.
void BossAI09::StartAtk03() {
    m_MoveDirection = glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI09__InAtk04 @ game_full.c:443625/443630.
// set_move_direction(Vector2.zero); owner then Instantiate<RGWeapon>. No RNG draw.
void BossAI09::InAtk04() {
    m_MoveDirection = glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: BossAI09__Scout @ game_full.c:443158.
// When dead(0x38) == 0 && dizzy(0xa1) == 0: target_obj(0x7c) = null. Gated out
// (dead or dizzy) writes nothing. No RNG draw.
void BossAI09::Scout() {
    if (!m_Dead && !m_Dizzy) {
        m_TargetCleared = true; // target_obj = null
    }
}

// FAITHFUL: BossAI09__Dizzy @ game_full.c:443693.
// When dead(0x38) == 0: dizzy(0xa1) = 1, owner SetBool("run", false). Gated out
// (dead) writes nothing. No RNG draw.
void BossAI09::EnterDizzy() {
    if (!m_Dead) {
        m_Dizzy = true;
    }
}

// FAITHFUL: BossAI09__OnGameStateChange @ game_full.c:443482.
// game_state == 1 && the_maker.the_room.state == 1 -> awake(0x18) = 1, then the
// virtual StartBossAI vtable call (owner). No RNG draw.
void BossAI09::OnGameStateChange(int gameState, bool roomReady) {
    if (gameState != 1) {
        return;
    }
    if (roomReady) {
        m_Awake = true; // owner: (**vtable+0x104)(this) == StartBossAI
    }
}

// FAITHFUL: BossAI09__FixedUpdate @ game_full.c:443067.
// Decoding param_1 as int*: param_1[6]=awake(0x18), param_1[0x20]=shooting(0x80),
// param_1[0xe]=dead(0x38). Inside if(awake!=0){ if(shooting==0){ if(dead!=0){
// *(byte*)(this+0x18)=0; ... } ... } ... }: the only brain field write is clearing
// awake(0x18)=0 when (awake && !shooting && dead). The surrounding
// FixedUpdateSeed, Rigidbody2D velocity integration (move_direction * speed) and
// the EnemyUpdate vtable(+0x11c) dispatch are owner-side, no RNG draw.
void BossAI09::FixedUpdateTick() {
    if (m_Awake && !m_Shooting && m_Dead) {
        m_Awake = false; // *(byte*)(this+0x18) = 0
    }
}

// FAITHFUL: BossAI09__BossAngry @ game_full.c:443413.
// angry(0xd0) = 1, shoot_cd(0x3c) *= 0.5, anim.set_speed(1.2f / 0x3f99999a),
// SetBool("angry", true). hp/max_hp < 0.5 is the standard GetHurt->BossAngry
// gate (GetHurt is in the shared DamageSystem). Once only. No RNG draw.
void BossAI09::OnHurt(int hpAfter, int maxHp) {
    if (m_Angry || maxHp <= 0) {
        return;
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction) {
        m_Angry = true;
        m_ShootCd *= kAngryShootCdScale; // shoot_cd halved (faster cadence)
        m_AnimSpeed = kAngryAnimSpeed;   // anim.set_speed(1.2f)
        // owner: anim.SetBool("angry", true) (StringLiteral_6560).
    }
}

} // namespace Game
