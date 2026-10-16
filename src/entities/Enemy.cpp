#include "entities/Enemy.hpp"
#include "game/RenderLayout.hpp"

#include <string>
#include <vector>

namespace Game {

namespace {

/// Build the bat animation frame paths from the resource root.
std::vector<std::string> MakeBatFrames(const std::string &resourceRoot) {
    std::vector<std::string> frames;
    frames.reserve(4);
    for (int i = 0; i < 4; ++i) {
        frames.push_back(resourceRoot + "/sprites/bat_" + std::to_string(i) +
                         ".png");
    }
    return frames;
}

} // namespace

Enemy::Enemy(const EnemyDef &def, const std::string &resourceRoot,
             glm::vec2 spawnPos, float detectRange, float attackRange,
             std::vector<std::string> frames, int frameIntervalMs)
    : m_AI(def, detectRange, attackRange),
      // SPRITE MANIFEST: the species' real frames (sprites.json) when the caller
      // resolved them; the bat placeholder only when no manifest entry exists.
      m_Anim(std::make_shared<Util::Animation>(
          frames.empty() ? MakeBatFrames(resourceRoot) : frames, true,
          frameIntervalMs, true)) {
    SetDrawable(m_Anim);
    m_Vis.SetClip(ActorPose::ActorClip::Idle, m_Anim, {});
    SetZIndex(RenderLayout::kZEnemy); // sorting layer 5 (Character), below the layer-6 overlays
    m_Transform.translation = spawnPos;
    m_PrevPos = spawnPos;
    SyncSprite(); // kSpriteScale + bottom-centre pivot until the scene installs the data anchor
}

void Enemy::UpdatePose(glm::vec2 position, float dtMs, float faceDx, bool locked) {
    const bool moving = ActorPose::IsMoving(position - m_PrevPos, dtMs);
    m_PrevPos = position;
    m_Transform.translation = position;
    m_Vis.SetFacing(ActorPose::ResolveFacing(m_Vis.Facing(), faceDx));
    m_Vis.SetClipState(ActorPose::SelectClip(false, moving));
    m_Vis.SetRingVisible(locked);
    SyncSprite();
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
    return Util::Collider::MakeCircle(Position(), m_Vis.Anchor().bodyRadiusPx);
}

const CombatStats &Enemy::Stats() const {
    return m_Stats;
}

} // namespace Game
