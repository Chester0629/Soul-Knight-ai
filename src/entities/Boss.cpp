#include "entities/Boss.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "Util/Image.hpp"

namespace Game {

namespace {
constexpr float kBossRadius = 28.0F;
constexpr float kBossZIndex = 6.0F;
constexpr float kBossScale = 1.5F;
constexpr int kBossFrames = 6;

std::vector<std::string> BossFrames(const std::string &resourceRoot) {
    std::vector<std::string> frames;
    frames.reserve(kBossFrames);
    for (int i = 0; i < kBossFrames; ++i) {
        frames.push_back(resourceRoot + "/sprites/boss01_" + std::to_string(i) +
                         ".png");
    }
    return frames;
}

glm::vec2 Norm(glm::vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 0.0F ? v / len : glm::vec2(0.0F, 0.0F);
}
} // namespace

Boss::Boss(const std::string &resourceRoot, glm::vec2 spawnPos, int maxHp,
           float shootCd, int seed)
    : m_Boss(shootCd), m_Stats{maxHp, maxHp, 0, 0, 0, 0},
      m_Anim(std::make_shared<Util::Animation>(BossFrames(resourceRoot), true,
                                               150, true)) {
    m_Boss.SetSeed(seed);
    SetDrawable(m_Anim);
    SetZIndex(kBossZIndex);
    m_Transform.translation = spawnPos;
    m_Transform.scale = glm::vec2(kBossScale, kBossScale);
}

glm::vec2 Boss::Think(float dtMs, glm::vec2 playerPos, bool &outShoot,
                      int &outAttack) {
    outShoot = false;
    outAttack = 0;
    m_ShootTimerMs -= dtMs;
    if (m_ShootTimerMs <= 0.0F) {
        outShoot = true;
        outAttack = m_Boss.ChooseAttack();
        // shoot_cd is in seconds and halves once angry (BossAI01.OnHurt).
        m_ShootTimerMs = m_Boss.ShootCd() * 1000.0F;
    }
    return Norm(playerPos - Position()); // chase the player
}

void Boss::TakeDamage(int dmg) {
    m_Stats.ApplyEnemyDamage(dmg);              // straight to HP (enemy rule)
    m_Boss.OnHurt(m_Stats.hp, m_Stats.maxHp);   // angry phase at < 50%
}

Util::Collider Boss::GetCollider() const {
    return Util::Collider::MakeCircle(m_Transform.translation, kBossRadius);
}

} // namespace Game
