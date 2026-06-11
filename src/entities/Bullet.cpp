/**
 * @file Bullet.cpp
 * @brief Implementation of the pooled Game::Bullet projectile entity.
 */

#include "entities/Bullet.hpp"

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "Util/Image.hpp"

namespace Game {

namespace {
/// Radius (in pixels) of the bullet's circular collider.
constexpr float kColliderRadius = 6.0f;
/// Z-index for rendering bullets above the floor and most actors.
constexpr float kBulletZIndex = 4.0f;
/// Conversion factor from milliseconds to seconds.
constexpr float kMsToSec = 1.0f / 1000.0f;
} // namespace

Bullet::Bullet(const std::string &resourceRoot) {
    SetDrawable(std::make_shared<Util::Image>(
        std::string(resourceRoot) + "/sprites/bullet_0.png"));
    SetZIndex(kBulletZIndex);
    SetVisible(false);
}

void Bullet::Init(glm::vec2 pos, glm::vec2 velocityPxPerSec, float lifetimeMs,
                  int damage, int camp) {
    Init(pos, velocityPxPerSec, lifetimeMs, damage, camp, 0.0F, 0, false, 0);
}

void Bullet::Init(glm::vec2 pos, glm::vec2 velocityPxPerSec, float lifetimeMs,
                  int damage, int camp, float repel, int critical,
                  bool canThrough, int pierce) {
    m_Transform.translation = pos;
    m_Velocity = velocityPxPerSec;
    m_LifeMs = lifetimeMs;
    m_Damage = damage;
    m_Camp = camp;
    m_Repel = repel;
    m_Critical = critical;
    m_CanThrough = canThrough;
    m_Pierce = pierce;
    m_Active = true;
    SetVisible(true);
}

bool Bullet::ConsumePierce() {
    // Non-piercing bullet despawns on its first hit.
    if (!m_CanThrough) {
        return true;
    }
    // Bool "infinite pierce" path (pierce budget 0): never despawns on hit.
    if (m_Pierce <= 0) {
        return false;
    }
    // Budgeted pierce: spend one pass-through; despawn when exhausted.
    --m_Pierce;
    return m_Pierce <= 0;
}

void Bullet::Update(float dtMs) {
    if (!m_Active) {
        return;
    }

    m_Transform.translation += m_Velocity * (dtMs * kMsToSec);
    m_LifeMs -= dtMs;

    if (m_LifeMs <= 0.0f) {
        Deactivate();
    }
}

bool Bullet::Active() const {
    return m_Active;
}

void Bullet::Deactivate() {
    m_Active = false;
    SetVisible(false);
}

int Bullet::Damage() const {
    return m_Damage;
}

int Bullet::Camp() const {
    return m_Camp;
}

Util::Collider Bullet::GetCollider() const {
    return Util::Collider::MakeCircle(m_Transform.translation, kColliderRadius);
}

} // namespace Game
