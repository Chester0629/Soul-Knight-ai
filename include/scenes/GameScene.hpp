#ifndef GAME_GAME_SCENE_HPP
#define GAME_GAME_SCENE_HPP

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Camera2D.hpp"
#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/ObjectPool.hpp"
#include "Util/Renderer.hpp"

#include "combat/WeaponInstance.hpp"
#include "data/GameData.hpp"
#include "entities/Bullet.hpp"
#include "entities/Enemy.hpp"
#include "entities/Player.hpp"
#include "ui/Hud.hpp"
#include "world/Room.hpp"

namespace Game {
/**
 * @class GameScene
 * @brief The playable combat scene: move, shoot, fight the bat enemy, take/deal
 *        damage, all inside a walled room with a follow camera and HUD.
 *
 * Converges the engine + game systems built across Phases 1-2: SceneManager
 * drives this scene; the player (CombatStats + WeaponInstance) fires pooled
 * Bullets at the mouse; the Enemy runs EnemyAI; Room walls block movement;
 * Camera2D follows; the HUD shows vitals.
 */
class GameScene : public Core::Scene {
public:
    GameScene();

    void OnEnter() override;
    void Update(float dtMs) override;
    void Render() override;

private:
    glm::vec2 AimDirection() const;
    void TryFirePlayerWeapon();
    void UpdateBullets(float dtMs);

    GameData m_Data;
    Core::Camera2D m_Camera;
    Util::Renderer m_Renderer;

    std::shared_ptr<Util::GameObject> m_Background;
    std::shared_ptr<Player> m_Player;
    std::shared_ptr<Enemy> m_Enemy;
    std::unique_ptr<Room> m_Room;
    std::unique_ptr<WeaponInstance> m_Weapon;

    Util::ObjectPool<Bullet> m_BulletPool;
    std::vector<std::shared_ptr<Bullet>> m_Bullets;

    Hud m_Hud;
    float m_EnergyRegenAccumMs = 0.0F;
};
} // namespace Game

#endif /* GAME_GAME_SCENE_HPP */
