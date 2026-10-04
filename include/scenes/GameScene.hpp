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
#include "Util/Image.hpp"
#include "Util/ObjectPool.hpp"
#include "Util/Renderer.hpp"

#include "sim/Simulation.hpp"
#include "sim/WorldInputs.hpp"
#include "sim/WorldCollision.hpp"
#include "data/GameData.hpp"
#include "data/LootTable.hpp"
#include "data/RGRandom.hpp"
#include "game/AudioPlayer.hpp"
#include "game/RunState.hpp"
#include "game/WaveScheduler.hpp"
#include "entities/Boss.hpp"
#include "entities/Bullet.hpp"
#include "entities/Chest.hpp"
#include "entities/CoinPickup.hpp"
#include "entities/VendingMachine.hpp"
#include "entities/Enemy.hpp"
#include "entities/Player.hpp"
#include "entities/WeaponPickup.hpp"
#include "ui/Hud.hpp"
#include "world/MapManager.hpp"
#include "world/RGBox.hpp"
#include "world/RGRoomX.hpp"
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
    /// WorldCollision: a consumed bullet at @p pos damages a destructible design
    /// box there; once broken the box stops blocking. No-op on walls/braziers.
    void DamageObstacle(glm::vec2 pos, float radius) override;

private:
    glm::vec2 AimDirection() const;
    /// @return true if a circle at @p pos / @p radius overlaps any room's walls.
    bool BlocksAny(glm::vec2 pos, float radius) const;

    GameData m_Data;
    Core::Camera2D m_Camera;
    Util::Renderer m_Renderer;

    std::shared_ptr<Player> m_Player;
    /// The hero's physical body radius (px) for wall/door blocking: the c##.prefab
    /// "collider" child CircleCollider2D radius * kCellPx (data). 16 px placeholder only
    /// while the hero has no collider data (logged once at load).
    float m_PlayerRadiusPx = 16.0F;
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
    /// M3e: the event room's statue (r_statue: statue_0N, TalkStatue item_value 15).
    struct StatueSpot {
        int index = 0;         ///< statue_index 1..7
        int cost = 0;          ///< shop.json statues[N].cost (TalkStatue.item_value)
        glm::vec2 pos{0.0F, 0.0F};
        bool used = false;
        std::shared_ptr<Util::Text> tag;
    };
    StatueSpot m_Statue;
    float m_StatueCdMs = 0.0F; ///< BuffStatue.cd countdown for the blessing
    /// SK_STATUE_WORSHIP=K test hook: stand at the statue and worship on frame K.
    long m_StatueWorshipFrame = -1;
    /// M3c: the shop room's machines (r_sell: vending_machine_i / _w, water_dispenser).
    std::vector<std::shared_ptr<VendingMachine>> m_Machines;
    /// SK_SHOP_BUY=K test hook (env, -1 = off): on frame K stand at the first machine
    /// and buy. NO-OP in normal play.
    long m_ShopBuyFrame = -1;
    /// Try to buy from @p m (use key / SK_SHOP_BUY). Applies the pot / drops the weapon.
    void Purchase(VendingMachine &m);
    /// B3: dropped coins / energy orbs (enemy death rewards) awaiting collection.
    std::vector<std::shared_ptr<CoinPickup>> m_Coins;
    /// B3: species per sim enemy index (parallel to m_Enemies) for the death reward roll.
    std::vector<const EnemyDef *> m_EnemyDefs;
    /// MAP M1: the original EnemyMaker spawns in PARTS -- `this_count` (= level*3)
    /// enemies per wave, the next wave 0.8 s after the previous one is dead
    /// (EnemyMaker.CreatePart / EnemyDead -> Invoke("CreatePart", 0.8f)). One queue
    /// per room: the species still to spawn and the room's spawn points.
    struct SpawnWaves {
        Game::WaveScheduler wave; ///< pending picks + the 0.8 s CreatePart timer (P2.3 extract)
        std::vector<glm::vec2> points;
        int serial = 0; ///< spawn ordinal (seed spread + point spread)
        /// T-29: EnemyMaker.part -- how many PARTS of this room's wave have been
        /// spawned. The original's CreatePart plays the show-up cue only when
        /// `part != 0`, i.e. from the 2nd part on (game_typed.c:78030-78082).
        int partsSpawned = 0;
    };
    std::vector<SpawnWaves> m_Waves; ///< indexed by room
    /// Instantiate one enemy of species @p id at @p at in room @p roomIndex.
    void SpawnEnemy(const std::string &id, glm::vec2 at, int roomIndex, int serial);
    /// Spawn the next part of a room's queue (up to waveSize).
    void SpawnNextWave(int roomIndex);
    /// Instantiate an already-taken wave batch at @p roomIndex's spawn points
    /// (the point-spread + logging half of the old SpawnNextWave body).
    void SpawnWaveBatch(int roomIndex, const std::vector<std::string> &batch);
    /// T-29: count down (and fire) the pending Invoke("ShowUpEffect", 0.8f) cues.
    void TickShowUpCues(float dtMs);
    /// T-29: remaining ms on each scheduled show-up cue (one per part >= 1 spawned).
    /// Scene-local and audio-only: it never touches the sim's clock or RNG.
    std::vector<float> m_ShowUpCuesMs;
    /// AUDIO (P6.7): this floor's LevelDef::biome, the audio.json bgm.by_biome key.
    const std::string &LevelBiome() const;
    /// AUDIO (T-35): audio.json ui_effects[@p key] -> clip name, with the single
    /// documented hardcoded fallback when the table itself has no entry.
    const std::string &UiClip(const char *key) const;
    bool PendingWaves() const;
    const EnemyDef *m_BossDef = nullptr; ///< B3: the floor's boss species (reward roll).
    const LevelDef *m_Level = nullptr;    ///< MAP M1: this floor's MapManager level (may be null).
    /// MAP M3: sprite cache for room-object skins (tiles/<stem>.png), by stem.
    std::unordered_map<std::string, std::shared_ptr<Util::Image>> m_SkinCache;
    // --- MAP M2: the transfer gate (res/item/transfer_gate.prefab, RGTransferGate).
    // The original never auto-advances: the player walks INTO the gate. It stands in
    // the type-2 special room (r_transgete, MapManager.type2_list) on mob floors and
    // appears in the boss room once the boss is dead on boss floors (type2_list =
    // r*_boss*). Usable once its room holds no live hostile / pending part. ---
    int m_GateRoom = -1;
    bool m_BossChestPending = false; ///< M3b (reserved: boss chest spawned on death)
    glm::vec2 m_GatePos{0.0F, 0.0F};
    bool m_GateActive = false;
    std::shared_ptr<Util::GameObject> m_Gate;      ///< effect04_5..12 loop (transfer_gate_1)
    std::shared_ptr<Util::GameObject> m_GateLight; ///< texiao_01 glow under it
    /// SK_GATE_TELEPORT=K test hook (env, -1 = off): put the player on the gate on
    /// frame K so the gate transition can be driven headlessly. NO-OP in normal play.
    long m_GateTeleportFrame = -1;
    /// SK_GATE_BESIDE=K test hook: put the player 64 px below the gate on frame K
    /// (outside the trigger) so the open gate can be screenshotted. NO-OP in play.
    long m_GateBesideFrame = -1;
    /// B3: the reward-roll stream (RGEController.rg_random analog), seeded per floor.
    RGRandom m_RewardRng;
    /// Door-gap colliders per room (sealed while the room is locked).
    std::vector<std::vector<Util::Collider>> m_RoomDoors;
    /// M3d: the door pieces of every door band (RGDoor d1..d5: door02 base, door01
    /// bar + shadow that only show while the room is sealed -- door_close2 raises
    /// the bar to +1 unit and enables it, door_open lowers and disables it).
    struct DoorPiece {
        int room = -1;
        std::shared_ptr<Util::GameObject> bar;
        std::shared_ptr<Util::GameObject> shadow;
        bool shown = false;
    };
    std::vector<DoorPiece> m_DoorPieces;
    /// SK_ROOM_TELEPORT="K,R" test hook: on frame K put the player in room R (its
    /// first spawn point) so a locked room can be screenshotted. NO-OP in play.
    long m_RoomTeleportFrame = -1;
    int m_RoomTeleportRoom = -1;
    /// Permanent corridor flank-wall colliders across the whole floor (the strip
    /// is walled on its long sides, open at both ends). Always block movement;
    /// unlike m_RoomDoors they are never sealed/unsealed (seam 2: corridors are
    /// neutral -- they are not part of any room's lock state).
    std::vector<Util::Collider> m_Corridors;
    /// Phase 3 design-room obstacle (prefab box obj_index 1-4, or brazier 11).
    /// Blocks movement while @c alive but is NOT on the RoomGen connectivity grid,
    /// so it blocks a body yet never makes a door unreachable. Boxes are
    /// destructible (RGBox HP); a brazier is a permanent conservative blocker.
    struct ObstacleBox {
        Util::Collider collider;
        RGBox box;                                    ///< durability (boxes only).
        std::shared_ptr<Util::GameObject> tile;       ///< placeholder skin, hidden on break.
        bool destructible = false;                    ///< box -> true; brazier -> false.
        int breakCoins = 0;                           ///< M3e minerals: copper coins dropped on break.
        bool alive = true;                            ///< false once broken (stops blocking).
    };
    std::vector<ObstacleBox> m_RoomObstacles;
    /// Phase 3 (#3a): per-room lifecycle state machine -- the SINGLE authority for
    /// door/lock state (Uncleared/Active/Cleared + door_open + ClearRoom reward
    /// gate). Indexed 1:1 with @ref m_Rooms. The sealed-door colliders in
    /// @ref m_RoomDoors are sealed iff the room's RGRoomX has door_open==false;
    /// there is NO separate writable lock field (the old m_LockedRoom is gone).
    std::vector<RGRoomX> m_RoomLife;

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
    /// B3: roll the killed hostile's reward (RGEController.GetReward) and drop the
    /// coins / energy at @p pos with a deterministic scatter.
    void SpawnReward(bool isBoss, std::uint32_t entityId, glm::vec2 pos);
    /// The hero this floor runs (B5): from the continuation on floors 1+, else the
    /// run's chosen hero (RunState::charId). Written into the floor-clear snapshot.
    std::string m_CharId = "c01";
    /// Deterministic stream for combat rolls (crit). The original rolls crit on
    /// Unity's global RNG; a seeded per-run stream keeps our combat replayable.
    RGRandom m_Rng;

    /// The deterministic sim this scene drives (Plan 4a). Constructed in OnEnter with
    /// (m_FloorSeed, this-as-WorldCollision). Move-deleted -> emplaced in place.
    std::optional<Sim::Simulation> m_Sim;
    /// Data-driven SFX/BGM (audio.json via GameData). Constructed in OnEnter
    /// with RESOURCE_DIR; std::optional because AudioPlayer isn't default-constructible.
    std::optional<Game::AudioPlayer> m_Audio;
    /// True once this floor's room-clear BGM (the "high"/combat variant, when
    /// the biome ships one) is the one currently looping; false = "low"/ambient.
    bool m_BgmHigh = false;
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
    /// SK_FORCE_KILL=K test hook (env, -1 = off): kill every hostile on frame K to
    /// drive the death-reward / kill-count paths headlessly. NO-OP in normal play.
    long m_ForceKillFrame = -1;
    long m_ForceKillEvery = -1; ///< SK_FORCE_KILL_EVERY=N: repeat the kill every N frames
    /// SK_FORCE_SKILL=K test hook (env, -1 = off): press the skill button on frame K
    /// (and hold-fire while it is active) to drive the skill path headlessly. NO-OP off.
    long m_ForceSkillFrame = -1;
    /// item 6 (R18/Codex block, H12): SK_FORCE_SKILL_PULSE=K1,K2 test hook (env,
    /// -1 = off): press the skill button for EXACTLY ONE frame at K1, and again for
    /// exactly one frame at K2 -- unlike SK_FORCE_SKILL (which HOLDS the button
    /// down from K onward forever), a one-frame pulse is released again the very
    /// next frame, so it produces a clean press-release-press-release sequence.
    /// That is what a TOGGLE-type skill (C04/C08) needs to prove BOTH "activated"
    /// (K1, a fresh edge) AND "ended" (K2, a second DISTINCT edge after the K1
    /// release) headlessly -- SK_FORCE_SKILL alone can only ever show "activated"
    /// for a toggle hero, because it never releases the button so a second press
    /// (the toggle's only end trigger) can never happen. NO-OP (both frames -1)
    /// unless set.
    long m_ForceSkillPulseFrame1 = -1;
    long m_ForceSkillPulseFrame2 = -1;
    /// Edge-detect InSkill() so the "Skill: <char> ended" trace (P3.3.a headless
    /// hook) logs exactly once per active-window close. NO-OP on behaviour --
    /// logging/bookkeeping only, mirrors the "Buff: ... ended" convention (P3.1).
    bool m_SkillWasActive = false;
    /// SK_FORCE_BUFF=K,<kind> test hook (env, -1 = off): apply <kind> ("fire",
    /// "poison", "ice", "electric", "armor", "changespeed") to the PLAYER on frame K
    /// (Simulation::ApplyBuff), so the buff-applied/ended log path (P3.1) can be
    /// proven headlessly. NOT bullet-triggered (that is P3.2); a direct API poke.
    long m_ForceBuffFrame = -1;
    /// SK_FORCE_HIT=K test hook (env, -1 = off): from frame K onward, override the
    /// aim direction to point straight at the nearest alive enemy/boss (recomputed
    /// every frame from the sim's own EnemyViews()/BossView()) and hold fire --
    /// deterministic headless proof that a bullet actually LANDS on a target
    /// (P3.2's auto-buff-on-hit path needs a real hit, not just a fired shot).
    /// Independent of SK_AUTOWALK/SK_AUTOFIRE (which aim along the travel
    /// direction, not at a target) -- combine with SK_AUTOWALK to first get the
    /// player within the target room. NO-OP (false) unless set.
    long m_ForceHitFrame = -1;
    std::string m_ForceBuffKind; ///< lower-cased kind name parsed from SK_FORCE_BUFF.
    /// SK_AUTOWALK test hook (env): when set, steer the player straight at
    /// m_AutoWalkTarget each frame to drive room->corridor->room traversal headlessly
    /// and log the room transitions. NO-OP in normal play (m_AutoWalk stays false).
    bool m_AutoWalk = false;
    bool m_AutoFire = false;                ///< SK_AUTOFIRE: fire along autowalk dir (test aid).
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
