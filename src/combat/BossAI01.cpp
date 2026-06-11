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

// FAITHFUL: RunReflection wander (Range(-1,1) x2, normalized).
glm::vec2 BossAI01::WanderDirection() {
    const float rx = m_Rng.Range(-1.0F, 1.0F);
    const float ry = m_Rng.Range(-1.0F, 1.0F);
    const float len = std::sqrt(rx * rx + ry * ry);
    return len > 0.0F ? glm::vec2(rx / len, ry / len)
                      : glm::vec2(0.0F, 0.0F);
}

} // namespace Game
