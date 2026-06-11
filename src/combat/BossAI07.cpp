#include "combat/BossAI07.hpp"

namespace Game {

// FAITHFUL: BossAI07__FixedUpdate @ game_full.c:441977
// Decomp HEAD: FixedUpdateSeed(); if(!awake[6]==0x18) return; if(dead[0xe]==0x38)
// { awake=0; get_transform } else vtable EnemyUpdate at *(this+0x11c) (jumptable
// at 0x550998 not recoverable). No RNG draw.
bool BossAI07::FixedUpdate() {
    // owner: RGEController.FixedUpdateSeed(this) (networked physics step).
    if (!m_Awake) {
        return false; // gated: AI not active yet
    }
    if (m_Dead) {
        m_Awake = false; // *(this+0x18) = 0
        // owner: get_transform (corpse settle); no further tick.
        return false;
    }
    // owner: vtable EnemyUpdate tail-call (this->...+0x11c) drives the attack FSM.
    return true;
}

// FAITHFUL: BossAI07__Scout @ game_full.c:442001
// if (dead(0x38)==0 && dizzy(0xa1)==0) { target_obj(0x7c)=0; get_transform }.
// No RNG draw.
bool BossAI07::Scout() {
    if (!m_Dead && !m_Dizzy) {
        m_TargetCleared = true; // *(this+0x7c) = 0 (target_obj = null)
        // owner: get_transform (re-acquire / face logic on the owner side).
        return true;
    }
    return false; // gated by dead/dizzy
}

// FAITHFUL: BossAI07__ShootReflection @ game_full.c:442035
// if (can_shoot(0x40)) { if (dead(0x38)==0 && dizzy(0xa1)==0) { can_shoot=0;
//   StartAtk01(); return; } }
// if (rg_random(0xc) != 0) { Range(scout_rate*0.5, scout_rate) [FLOAT];
//   Invoke("...", delay); return; }
// The attack-dispatch path takes NO draw; only the re-Invoke path draws once.
bool BossAI07::ShootReflection(float &reInvokeDelay) {
    if (m_CanShoot) {
        if (!m_Dead && !m_Dizzy) {
            m_CanShoot = false; // *(this+0x40) = 0
            // owner: StartAtk01() -> anim.SetTrigger("...") (StringLiteral_6578).
            return true; // attack dispatched; stream untouched (no draw)
        }
    }
    // rg_random(0xc) live -> schedule next ShootReflection. Range(float) inclusive.
    reInvokeDelay =
        m_Rng.Range(m_ScoutRate * kReinvokeDelayLowScale, m_ScoutRate);
    // owner: Invoke("ShootReflection", reInvokeDelay) (StringLiteral_6549).
    return false;
}

// FAITHFUL: BossAI07__BossAngry @ game_full.c:442087
// angry(0xb0)=1; shoot_cd(0x3c) *= 0.5; anim.set_speed(1.2); anim.SetBool("angry",1).
// Only the two field writes are pure logic; the anim calls are owner-side. The
// decomp has NO angry latch: BOTH writes run unconditionally on every call, so a
// second invocation halves shoot_cd again.
void BossAI07::BossAngry() {
    m_Angry = true;                  // *(this+0xb0) = 1 (unconditional)
    m_ShootCd *= kAngryShootCdScale; // *(this+0x3c) *= 0.5 (re-halved each call)
    // owner: anim.set_speed(1.2f) (0x3f99999a); anim.SetBool("angry", true).
}

// FAITHFUL: BossAI07__OnGameStateChange @ game_full.c:442220
// if (game_state != 1) return; if (the_maker(0x94).the_room(0x28).field(0x10)==1)
//   { awake[6]=0x18 = 1; vtable StartBossAI at *(this+0x104) }.
// (jumptable at 0x552284 not recoverable). No RNG draw.
bool BossAI07::OnGameStateChange(int gameState, bool roomReady) {
    if (gameState != kGameStateStart) {
        return false;
    }
    if (roomReady) {
        m_Awake = true; // *(this+0x18) = 1
        // owner: vtable StartBossAI tail-call (this->...+0x104).
        return true;
    }
    return false;
}

// FAITHFUL: BossAI07__InAtk01 @ game_full.c:442249
// if (rg_random(0xc) != 0) Range(0, 100) [INT, max exclusive]. No field write.
int BossAI07::InAtk01() {
    return m_Rng.Range(0, kInAtk01RollCeiling);
}

// FAITHFUL: BossAI07__CreateBullet1 @ game_full.c:442262 and
//           BossAI07__CreateBullet4 @ game_full.c:442370 (identical brain).
// owner: RGMusicManager.PlayEffect(boss_clip[6]) gated by boss_clip(0xb8).Length
// >= 3 (decomp: *(boss_clip+0xc) < 3 -> bounds throw). The single RNG draw is
// Range(0, 0x14) == Range(0, 20) [INT, max exclusive]. No field write.
int BossAI07::CreateBulletRoll() {
    // owner gate: boss_clip.Length >= kBossClipMinLen (else IL2CPP bounds throw);
    // owner: RGMusicManager.PlayEffect(boss_clip[index]).
    return m_Rng.Range(0, kCreateBulletRollCeiling);
}

// FAITHFUL: BossAI07__CreateBullet2 @ game_full.c:442297
// uVar1 = angry(0xb0) ? 0xc : 8; loop count = uVar1 >> 1 (4 calm / 6 angry).
// owner: get_transform + bullet02 ring spawn. No RNG draw, no field write.
int BossAI07::CreateBullet2RingCount() const {
    return m_Angry ? kBullet2RingAngry : kBullet2RingCalm;
}

} // namespace Game
