#include "entities/Enemy.hpp"

#include <string>
#include <vector>

namespace Game {

namespace {

/// Build <name>_0..count-1 frame paths. count=4 is safe for every enemy set
/// (bat has 4 frames; every enemyNN has >= 6), so a per-type sprite never indexes
/// past its frame list.
std::vector<std::string> MakeFrames(const std::string &resourceRoot,
                                    const std::string &name, int count) {
    std::vector<std::string> frames;
    frames.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        frames.push_back(resourceRoot + "/sprites/" + name + "_" +
                         std::to_string(i) + ".png");
    }
    return frames;
}

} // namespace

Enemy::Enemy(const EnemyDef &def, const std::string &resourceRoot,
             glm::vec2 spawnPos, float detectRange, float attackRange,
             const std::string &spriteName)
    : m_AI(def, detectRange, attackRange),
      m_Anim(std::make_shared<Util::Animation>(
          MakeFrames(resourceRoot, spriteName, 4), true, 120, true)) {
    SetDrawable(m_Anim);
    SetZIndex(5);
    m_Transform.translation = spawnPos;
}

EnemyAI::Decision Enemy::Think(float dtMs, glm::vec2 playerPos) {
    return m_AI.Update(dtMs, Position(), playerPos);
}

void Enemy::ApplyMove(glm::vec2 dir, float dtMs) {
    m_Transform.translation += dir * m_Speed * (dtMs / 1000.0F);
}

void Enemy::TakeDamage(int dmg) {
    m_Stats.TakeDamage(dmg);
}

bool Enemy::IsDead() const {
    return m_Stats.IsDead();
}

glm::vec2 Enemy::Position() const {
    return m_Transform.translation;
}

Util::Collider Enemy::GetCollider() const {
    return Util::Collider::MakeCircle(Position(), 16.0F);
}

const CombatStats &Enemy::Stats() const {
    return m_Stats;
}

} // namespace Game
