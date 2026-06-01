#include "scenes/GameScene.hpp"

#include <cmath>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "Core/Context.hpp"

#include "Util/Collider.hpp"
#include "Util/Image.hpp"
#include "Util/Input.hpp"
#include "Util/Keycode.hpp"
#include "Util/Logger.hpp"
#include "Util/Position.hpp"
#include "Util/TransformUtils.hpp"

namespace Game {
namespace {
constexpr float kPlayerRadius = 16.0F;
constexpr float kBulletSpeedScale = 15.0F; // data bullet_speed -> pixels/second
constexpr float kBulletLifeMs = 1500.0F;
constexpr float kEnemyBulletSpeed = 300.0F;
constexpr float kEnergyRegenMs = 400.0F; // +1 energy per interval
constexpr int kEnemyContactDamage = 1;

glm::vec2 Normalize(glm::vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 0.0F ? v / len : glm::vec2(0.0F, 0.0F);
}
} // namespace

GameScene::GameScene()
    : m_BulletPool(0, [] {
          return std::make_shared<Bullet>(std::string(RESOURCE_DIR));
      }) {}

void GameScene::OnEnter() {
    const std::string root = RESOURCE_DIR;
    m_Data.LoadAll(root);

    m_Background = std::make_shared<Util::GameObject>();
    m_Background->SetDrawable(
        std::make_shared<Util::Image>(root + "/sprites/Background.png"));
    m_Background->SetZIndex(0.0F);

    m_Player = std::make_shared<Player>(m_Data.PlayerTemplate(), root);

    m_Room = std::make_unique<Room>(glm::vec2(0.0F, 0.0F),
                                    glm::vec2(1000.0F, 600.0F), 40.0F);

    if (const EnemyDef *edef = m_Data.FindEnemy("EnemyAI01")) {
        m_Enemy = std::make_shared<Enemy>(*edef, root, glm::vec2(300.0F, 150.0F),
                                          450.0F, 260.0F);
    }

    if (const WeaponDef *wdef = m_Data.FindWeapon("Gun001")) {
        m_Weapon = std::make_unique<WeaponInstance>(*wdef);
    }

    m_Renderer.AddChild(m_Background);
    m_Renderer.AddChild(m_Player);
    if (m_Enemy != nullptr) {
        m_Renderer.AddChild(m_Enemy);
    }

    m_Camera.SetPosition(m_Player->Position());
    LOG_INFO("GameScene: player + enemy + room + weapon ready");
}

glm::vec2 GameScene::AimDirection() const {
    const glm::vec2 cursor = Util::Input::GetCursorPosition(); // screen px (y down)
    const Util::PTSDPosition w = Util::PTSDPosition::FromSDL(
        static_cast<int>(cursor.x), static_cast<int>(cursor.y));
    // The camera center maps to the screen center, so the world point under the
    // cursor is the camera position plus the cursor's center-origin offset.
    const glm::vec2 worldCursor = m_Camera.GetPosition() + glm::vec2(w.x, w.y);
    return Normalize(worldCursor - m_Player->Position());
}

void GameScene::TryFirePlayerWeapon() {
    if (m_Weapon == nullptr || !m_Weapon->Ready()) {
        return;
    }
    if (!m_Player->Stats().SpendEnergy(m_Weapon->EnergyCost())) {
        return; // not enough energy
    }
    m_Weapon->TryFire();

    const float speed = m_Weapon->Def().bulletSpeed * kBulletSpeedScale;
    auto bullet = m_BulletPool.Acquire();
    bullet->Init(m_Player->Position(), AimDirection() * speed, kBulletLifeMs,
                 m_Weapon->Def().atk, /*camp=*/0);
    m_Renderer.AddChild(bullet);
    m_Bullets.push_back(bullet);
}

void GameScene::UpdateBullets(float dtMs) {
    for (auto it = m_Bullets.begin(); it != m_Bullets.end();) {
        auto &bullet = *it;
        bullet->Update(dtMs);

        if (bullet->Active()) {
            if (bullet->Camp() == 0 && m_Enemy != nullptr && !m_Enemy->IsDead()) {
                if (Util::Overlap(bullet->GetCollider(),
                                  m_Enemy->GetCollider())) {
                    m_Enemy->TakeDamage(bullet->Damage());
                    bullet->Deactivate();
                }
            } else if (bullet->Camp() == 1) {
                const Util::Collider playerHit =
                    Util::Collider::MakeCircle(m_Player->Position(),
                                               kPlayerRadius);
                if (Util::Overlap(bullet->GetCollider(), playerHit)) {
                    m_Player->Stats().TakeDamage(bullet->Damage());
                    bullet->Deactivate();
                }
            }
        }

        if (!bullet->Active()) {
            m_Renderer.RemoveChild(bullet);
            m_BulletPool.Release(bullet);
            it = m_Bullets.erase(it);
        } else {
            ++it;
        }
    }
}

void GameScene::Update(float dtMs) {
    if (Util::Input::IsKeyUp(Util::Keycode::ESCAPE) || Util::Input::IfExit()) {
        Core::Context::GetInstance()->SetExit(true);
        return;
    }

    // --- Player movement with axis-separated wall sliding ---
    const glm::vec2 before = m_Player->Position();
    m_Player->Update(dtMs);
    const glm::vec2 after = m_Player->Position();
    if (m_Room != nullptr) {
        glm::vec2 resolved = before;
        if (!m_Room->Blocks(glm::vec2(after.x, before.y), kPlayerRadius)) {
            resolved.x = after.x;
        }
        if (!m_Room->Blocks(glm::vec2(resolved.x, after.y), kPlayerRadius)) {
            resolved.y = after.y;
        }
        m_Player->m_Transform.translation = resolved;
    }

    // --- Energy regen ---
    m_EnergyRegenAccumMs += dtMs;
    while (m_EnergyRegenAccumMs >= kEnergyRegenMs) {
        m_EnergyRegenAccumMs -= kEnergyRegenMs;
        m_Player->Stats().AddEnergy(1);
    }

    // --- Weapon fire ---
    if (m_Weapon != nullptr) {
        m_Weapon->Update(dtMs);
    }
    if (Util::Input::IsKeyPressed(Util::Keycode::MOUSE_LB)) {
        TryFirePlayerWeapon();
    }

    // --- Enemy AI ---
    if (m_Enemy != nullptr && !m_Enemy->IsDead()) {
        const EnemyAI::Decision decision =
            m_Enemy->Think(dtMs, m_Player->Position());
        const glm::vec2 enemyBefore = m_Enemy->Position();
        m_Enemy->ApplyMove(decision.moveDir, dtMs);
        if (m_Room != nullptr &&
            m_Room->Blocks(m_Enemy->Position(), kPlayerRadius)) {
            m_Enemy->m_Transform.translation = enemyBefore; // wall blocks enemy
        }
        if (decision.shouldShoot) {
            const glm::vec2 dir =
                Normalize(m_Player->Position() - m_Enemy->Position());
            auto bullet = m_BulletPool.Acquire();
            bullet->Init(m_Enemy->Position(), dir * kEnemyBulletSpeed,
                         kBulletLifeMs, kEnemyContactDamage, /*camp=*/1);
            m_Renderer.AddChild(bullet);
            m_Bullets.push_back(bullet);
        }
    }

    UpdateBullets(dtMs);

    // --- Deaths ---
    if (m_Enemy != nullptr && m_Enemy->IsDead()) {
        m_Renderer.RemoveChild(m_Enemy);
        m_Enemy.reset();
        LOG_INFO("Enemy defeated!");
    }
    if (m_Player->Stats().IsDead()) {
        LOG_INFO("Player defeated -- exiting");
        Core::Context::GetInstance()->SetExit(true);
        return;
    }

    // --- Camera follow ---
    m_Camera.Follow(m_Player->Position(), 0.1F);
    m_Camera.Update(dtMs);
}

void GameScene::Render() {
    Util::SetActiveViewMatrix(m_Camera.GetViewMatrix());
    m_Renderer.Update();
    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen-space from here on
    m_Hud.Draw(m_Player->Stats());
}

} // namespace Game
