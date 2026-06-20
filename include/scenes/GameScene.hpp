#ifndef GAME_GAME_SCENE_HPP
#define GAME_GAME_SCENE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Camera2D.hpp"
#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/ObjectPool.hpp"
#include "Util/Renderer.hpp"

#include "sim/Simulation.hpp"
#include "sim/WorldInputs.hpp"
#include "sim/WorldCollision.hpp"
#include "data/GameData.hpp"
#include "data/LootTable.hpp"
#include "data/RGRandom.hpp"
#include "game/RunState.hpp"
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
class RunController; // forward: back-pointer only; full type included in GameScene.cpp.
/**
 * @class GameScene
 * @brief The playable combat scene: move, shoot, fight the bat enemy, take/deal
 *        damage, all inside a walled room with a follow camera and HUD.
 *
 * Converges the engine + game systems built across Phases 1-2: SceneManager
 * drives this scene; the sim (Sim::Simulation) handles combat and fires pooled
 * Bullets; the Enemy/Boss run inside the sim; Room walls block movement;
 * Camera2D follows; the HUD shows vitals.
 */
class GameScene : public Core::Scene, public Sim::WorldCollision {
public:
    /// @param floorSeed  per-floor base seed = PerFloorSeed(runSeed, floorIndex);
    ///                   roots every deterministic stream this floor owns.
    /// @param floorIndex 0-based floor index (threaded to RoomGen for fidelity).
    /// @param carried    player state from the previous floor; empty on floor 0
    ///                   (use the character template) -- applied in OnEnter.
    /// @param run        owning RunController back-pointer; the scene signals
    ///                   `run->OnFloorCleared(...)` from Update on whole-floor clear.
    ///                   The controller outlives every scene that points at it (D1).
    GameScene(int floorSeed, int floorIndex,
              std::optional<PlayerContinuation> carried, RunController *run);

    void OnEnter() override;
    void OnExit() override; // logs on pop/replace -- proves no scene leak (step 2b).
    void Update(float dtMs) override;
    void Render() override;

    /// WorldCollision: a circle at @p pos / @p radius overlaps a wall or sealed door.
    bool Blocks(glm::vec2 pos, float radius) const override { return BlocksAny(pos, radius); }

private:
    glm::vec2 AimDirection() const;
    /// @return true if a circle at @p pos / @p radius overlaps any room's walls.
    bool BlocksAny(glm::vec2 pos, float radius) const;

    GameData m_Data;
    Core::Camera2D m_Camera;
    Util::Renderer m_Renderer;

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
    /// Permanent corridor flank-wall colliders across the whole floor (the strip
    /// is walled on its long sides, open at both ends). Always block movement;
    /// unlike m_RoomDoors they are never sealed/unsealed (seam 2: corridors are
    /// neutral -- they are not part of any room's lock state).
    std::vector<Util::Collider> m_Corridors;
    /// Index of the active, uncleared room whose doors are sealed, else -1.
    int m_LockedRoom = -1;

    Util::ObjectPool<Bullet> m_BulletPool;

    Hud m_Hud;

    /// Per-floor base seed = PerFloorSeed(runSeed, floorIndex). Roots every
    /// deterministic stream this floor owns (combat crit rolls, the enemy AI
    /// stream, MapManager/RoomGen, enemy/boss/weapon seeds). Differs per floor so
    /// floors generate differently -- the only in-game-observable floorIndex effect.
    int m_FloorSeed;
    /// 0-based floor index. Threaded into RoomGen::Options.floorIndex for fidelity,
    /// but INERT under this scene's randomRoom=false build path (RoomGen reads it
    /// only inside the randomRoom==true branch; see GameScene.cpp + RUN_LOOP_PLAN
    /// section 4b). The live per-floor difference comes from m_FloorSeed, not this field.
    int m_FloorIndex;
    /// Player state carried from the previous floor; empty on floor 0 (use the
    /// character template). Applied ONCE in OnEnter: overwrite Stats + re-equip.
    std::optional<PlayerContinuation> m_Carried;
    /// Owning RunController (back-pointer). Used ONLY to signal whole-floor clear
    /// (`m_Run->OnFloorCleared(...)`); the controller outlives this scene (D1), and
    /// the scene's dtor never dereferences it.
    RunController *m_Run;
    /// Deterministic stream for combat rolls (crit). The original rolls crit on
    /// Unity's global RNG; a seeded per-run stream keeps our combat replayable.
    RGRandom m_Rng;

    /// The deterministic sim this scene drives (Plan 4a). Constructed in OnEnter with
    /// (m_FloorSeed, this-as-WorldCollision). Move-deleted -> emplaced in place.
    std::optional<Sim::Simulation> m_Sim;
    /// Pooled Bullet VIEWS keyed by the sim BulletState id (render mirror only).
    std::unordered_map<std::uint32_t, std::shared_ptr<Bullet>> m_BulletViews;
    /// Energy spent per player shot (the equipped WeaponDef.consume).
    int m_WeaponEnergyCost = 1;
    /// Bumps each weapon swap so the rebuilt WeaponController gets a fresh
    /// deterministic seed. Floor-LOCAL: a fresh floor correctly starts it at 0
    /// because m_FloorSeed already differs per floor (RUN_LOOP_PLAN section gap-1), so the
    /// per-equip `m_FloorSeed + 5 + m_WeaponSwaps` seeds never collide across floors.
    int m_WeaponSwaps = 0;
    /// The currently-equipped WeaponDef id, set on EVERY equip (initial + pickup),
    /// so it can be snapshotted into a PlayerContinuation at floor-clear. This is
    /// the weapon-id carry vehicle: it replaces the id being implicit in the
    /// EquipWeapon calls (RUN_LOOP_PLAN section 2 / D3 supplement 4).
    std::string m_CurrentWeaponId;

    // --- Forward run loop (step 2a): whole-floor clear -> transition ---
    /// True once >= 1 hostile has existed on this floor (set in OnEnter). Arms the
    /// clear check so a degenerate hostile-less floor can never auto-skip on frame 0
    /// (boss spawns every floor today, so this is normally true -- a safety guard).
    bool m_HadHostiles = false;
    /// Set true the instant we signal floor-clear, so OnFloorCleared fires EXACTLY
    /// once (the deferred Replace tears this scene down right after this Update).
    bool m_Transitioning = false;
    /// Frame counter for the SK_FORCE_CLEAR test hook only.
    int m_Frame = 0;
    /// SK_FORCE_CLEAR=K test hook (env, -1 = off): force the clear path on frame K so
    /// the play->clear->next-floor transition can be driven headlessly without combat
    /// or input. NO-OP in normal play (mirrors the SK_MAX_FRAMES/SK_SHOT hooks).
    long m_ForceClearFrame = -1;
    /// SK_FORCE_DIE=K test hook (env, -1 = off): kill the player on frame K to drive
    /// the death->EndScene path headlessly. NO-OP in normal play.
    long m_ForceDieFrame = -1;
    /// SK_FORCE_SKILL=K test hook (env, -1 = off): press the skill button on frame K
    /// (and hold-fire while it is active) to drive the skill path headlessly. NO-OP off.
    long m_ForceSkillFrame = -1;
    /// SK_AUTOWALK test hook (env): when set, steer the player straight at
    /// m_AutoWalkTarget each frame to drive room->corridor->room traversal headlessly
    /// and log the room transitions. NO-OP in normal play (m_AutoWalk stays false).
    bool m_AutoWalk = false;
    glm::vec2 m_AutoWalkTarget{0.0F, 0.0F}; ///< world centre of the adjacent target room.
    int m_AutoWalkRoom = -1;                ///< index of the target adjacent room.
    int m_LastRoomId = -2;                  ///< last logged playerRoomId (-2 = unset).

    void SyncBulletViews();             // defined in Task 2.
    bool RoomHasLiveHostile(int roomId) const; // defined in Task 2.

    // --- A (presentation): drain the sim's anim/sfx cues each frame and play them ---
    /// Drain Simulation::DrainEvents() and turn each cue into a transient effect + SFX,
    /// resolving the entity's world position from the sim views. @p playerPos is this
    /// frame's resolved player position (kPlayerViewId events anchor there).
    void ConsumeSimEvents(glm::vec2 playerPos);
    /// Spawn a one-shot effect animation at @p pos that auto-removes after @p lifeMs.
    void SpawnEffect(std::vector<std::string> frames, glm::vec2 pos, float lifeMs);
    /// Live transient presentation effects (muzzle/hit/death): {object, remaining ms}.
    std::vector<std::pair<std::shared_ptr<Util::GameObject>, float>> m_Effects;
};
} // namespace Game

#endif /* GAME_GAME_SCENE_HPP */
