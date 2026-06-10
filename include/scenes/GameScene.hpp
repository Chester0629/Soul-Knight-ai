#ifndef GAME_GAME_SCENE_HPP
#define GAME_GAME_SCENE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Camera2D.hpp"
#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/ObjectPool.hpp"
#include "Util/Renderer.hpp"

#include "combat/Damage.hpp"
#include "combat/WeaponInstance.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldInputs.hpp"
#include "sim/WorldCollision.hpp"
#include "data/GameData.hpp"
#include "data/LootTable.hpp"
#include "data/RGRandom.hpp"
#include "entities/Boss.hpp"
#include "entities/Bullet.hpp"
#include "entities/Chest.hpp"
#include "entities/Enemy.hpp"
#include "entities/Player.hpp"
#include "entities/WeaponPickup.hpp"
#include "ui/Hud.hpp"
#include "world/MapManager.hpp"
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
class GameScene : public Core::Scene, public Sim::WorldCollision {
public:
    GameScene();

    void OnEnter() override;
    void Update(float dtMs) override;
    void Render() override;

    /// WorldCollision: a circle at @p pos / @p radius overlaps a wall or sealed door.
    bool Blocks(glm::vec2 pos, float radius) const override { return BlocksAny(pos, radius); }

private:
    glm::vec2 AimDirection() const;
    void TryFirePlayerWeapon();
    void UpdateBullets(float dtMs);
    /// @return true if a circle at @p pos / @p radius overlaps any room's walls.
    bool BlocksAny(glm::vec2 pos, float radius) const;

    GameData m_Data;
    Core::Camera2D m_Camera;
    Util::Renderer m_Renderer;

    std::shared_ptr<Util::GameObject> m_Background;
    std::shared_ptr<Player> m_Player;
    /// One Enemy per spawned enemy across the whole floor.
    std::vector<std::shared_ptr<Enemy>> m_Enemies;
    /// Bosses on the floor (one, in the room farthest from the start).
    std::vector<std::shared_ptr<Boss>> m_Bosses;
    /// One Room (collision) per generated room cell on the floor.
    std::vector<std::unique_ptr<Room>> m_Rooms;
    /// Rendered obstacle/wall tiles across all rooms.
    std::vector<std::shared_ptr<Util::GameObject>> m_RoomTiles;
    /// Loot tables (droptables.json) for chest rolls.
    LootTable m_Loot;
    /// Chests placed across the floor (open once on proximity).
    std::vector<std::shared_ptr<Chest>> m_Chests;
    /// Dropped weapon pickups awaiting collection.
    std::vector<std::shared_ptr<WeaponPickup>> m_Pickups;
    /// Door-gap colliders per room (sealed while the room is locked).
    std::vector<std::vector<Util::Collider>> m_RoomDoors;
    /// Index of the active, uncleared room whose doors are sealed, else -1.
    int m_LockedRoom = -1;
    std::unique_ptr<WeaponInstance> m_Weapon;

    Util::ObjectPool<Bullet> m_BulletPool;
    std::vector<std::shared_ptr<Bullet>> m_Bullets;

    Hud m_Hud;
    float m_EnergyRegenAccumMs = 0.0F;

    /// Run seed: roots every deterministic stream this scene owns (combat crit
    /// rolls, the enemy AI stream, and -- once wired -- dungeon generation).
    int m_RunSeed = 20240607;
    /// Deterministic stream for combat rolls (crit). The original rolls crit on
    /// Unity's global RNG; a seeded per-run stream keeps our combat replayable.
    RGRandom m_Rng;

    /// The deterministic sim this scene drives (Plan 4a). Constructed in OnEnter with
    /// (m_RunSeed, this-as-WorldCollision). Move-deleted -> emplaced in place.
    std::optional<Sim::Simulation> m_Sim;
    /// Pooled Bullet VIEWS keyed by the sim BulletState id (render mirror only).
    std::unordered_map<std::uint32_t, std::shared_ptr<Bullet>> m_BulletViews;
    /// Energy spent per player shot (the equipped WeaponDef.consume).
    int m_WeaponEnergyCost = 1;
    /// Bumps each weapon swap so the rebuilt WeaponController gets a fresh deterministic seed.
    int m_WeaponSwaps = 0;
    void SyncBulletViews();             // defined in Task 2.
    bool RoomHasLiveHostile(int roomId) const; // defined in Task 2.
};
} // namespace Game

#endif /* GAME_GAME_SCENE_HPP */
