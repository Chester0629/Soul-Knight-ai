#include "combat/BossAINian.hpp"

namespace Game {

BossAINian::BossAINian(float baseShootCd) : m_ShootCd(baseShootCd) {}

// FAITHFUL: BossAINian.TurnInvisible @ game_full.c:960657
// Guarded by the invisible flag (0xd4): only phases out when currently visible,
// fades alpha to 0.5 and arms the 2s BackInvisible callback.
bool BossAINian::TurnInvisible() {
    if (m_Invisible) {
        return false;
    }
    m_Invisible = true;
    m_InvisibleTimer = kInvisibleDuration; // Invoke("BackInvisible", 2f)
    return true;
}

// FAITHFUL: BossAINian.BackInvisible @ game_full.c:960690 (timer-driven).
// When the 2s window elapses, restore full alpha and become hurtable again.
bool BossAINian::Tick(float dt) {
    if (!m_Invisible) {
        return false;
    }
    m_InvisibleTimer -= dt;
    if (m_InvisibleTimer <= 0.0F) {
        m_InvisibleTimer = 0.0F;
        m_Invisible = false; // alpha back to 1.0, hurtable
        return true;
    }
    return false;
}

// FAITHFUL: BossAINian.GetHurt @ game_full.c:960290
// Returns early (no damage, no angry check) when invisible (0xd4); otherwise
// applies the hit and enters angry once at hp/max_hp < 0.5 (0xb0).
bool BossAINian::OnHurt(int hpAfter, int maxHp) {
    if (m_Invisible) {
        return true; // absorbed: hit ignored while phased out
    }
    if (m_Angry || maxHp <= 0) {
        return false;
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction) {
        // FAITHFUL: BossAINian.BossAngry @ game_full.c:960339
        m_Angry = true;
        m_ShootCd *= kAngryShootCdScale; // shoot_cd *= 0.5
        m_AnimSpeed = kAngryAnimSpeed;   // anim.speed = 1.2f
    }
    return false;
}

// Deterministic invisibility decision. The exact ShootReflection bucketing is
// inlined in the il2cpp build (manual_flags); a single Range(0,100) draw keeps
// the stream in lockstep.
bool BossAINian::RollInvisibility(int chancePercent) {
    const int roll = m_Rng.Range(0, 100);
    return roll < chancePercent;
}

} // namespace Game
