#include "combat/BossAI01.hpp"

#include <cmath>

namespace Game {

BossAI01::BossAI01(float baseShootCd) : m_ShootCd(baseShootCd) {}

// FAITHFUL: BossAI01.GetHurt -> BossAngry (hp/max_hp < 0.5, once).
void BossAI01::OnHurt(int hpAfter, int maxHp) {
    if (m_Angry || maxHp <= 0) {
        return;
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction) {
        m_Angry = true;
        m_ShootCd *= kAngryShootCdScale; // attacks ~2x faster
        m_AnimSpeed = kAngryAnimSpeed;   // anim.speed = 1.2f
    }
}

// FAITHFUL: ShootReflection's rg_random.Range(0, 100) attack roll.
int BossAI01::ChooseAttack() {
    const int roll = m_Rng.Range(0, 100);
    int idx = roll / (100 / kAttackCount); // 4 equal buckets (TODO[verify])
    if (idx >= kAttackCount) {
        idx = kAttackCount - 1;
    }
    return idx;
}

// FAITHFUL: RunReflection wander (no-target branch: Range(-1,1) x2, normalized).
glm::vec2 BossAI01::WanderDirection() {
    const float rx = m_Rng.Range(-1.0F, 1.0F);
    const float ry = m_Rng.Range(-1.0F, 1.0F);
    const float len = std::sqrt(rx * rx + ry * ry);
    return len > 0.0F ? glm::vec2(rx / len, ry / len)
                      : glm::vec2(0.0F, 0.0F);
}

// FAITHFUL: RunReflection with-target move decision (game_named.c:122666-122723).
// Draw order: threshold Range(5,10) FIRST, then selector Range(0,10) -- both int draws.
glm::vec2 BossAI01::ChaseMoveDecision(glm::vec2 chaseDir, float dist) {
    const int thr = m_Rng.Range(5, 10);  // retreat-distance threshold (drawn first), 5..9
    const int roll = m_Rng.Range(0, 10); // switch selector (drawn second), 0..9
    if (roll < 6) {
        // ~60%: chase the player, but retreat (negate) when closer than the threshold.
        return (dist < static_cast<float>(thr)) ? glm::vec2(-chaseDir.x, -chaseDir.y) : chaseDir;
    }
    if (roll < 8) {
        return glm::vec2(-chaseDir.x, chaseDir.y); // ~20%: strafe, mirror X
    }
    return glm::vec2(chaseDir.x, -chaseDir.y); // ~20%: strafe, mirror Y
}

} // namespace Game
