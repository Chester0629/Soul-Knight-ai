#ifndef GAME_SIM_SIMULATION_HPP
#define GAME_SIM_SIMULATION_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "combat/CharSkillC01.hpp"
#include "combat/CombatStats.hpp"
#include "data/GameData.hpp"
#include "data/RGRandom.hpp"
#include "sim/BossController.hpp"
#include "sim/BulletState.hpp"
#include "sim/EnemyController.hpp"
#include "sim/FireIntent.hpp"
#include "sim/FireSystem.hpp"
#include "sim/FixedClock.hpp"
#include "sim/Scheduler.hpp"
#include "sim/SimEvent.hpp"
#include "sim/WeaponController.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

namespace Game::Sim {

/// The deterministic, headless sim root. Owns the clock/scheduler/firesystem, the
/// Enemy/Boss/Weapon controllers, the bullet list, and a dedicated hit-resolution RNG.
/// Move/copy DELETED: controllers + scheduler hold raw pointers to this object's
/// members, so it must never relocate after Activate.
class Simulation {
public:
    /// A render-facing snapshot of one controller's logical state.
    struct EntityView {
        std::uint32_t id = 0; ///< stable within a run (enemy index / boss sentinel).
        glm::vec2 pos{0.0F, 0.0F};
        glm::vec2 facing{1.0F, 0.0F}; ///< unit heading: enemy move-dir / boss chase-dir
                                      ///< (retains last heading while still).
        int hp = 0;
        int maxHp = 0;
        bool alive = false;
        int roomId = -1;
    };

    static constexpr std::uint32_t kBossViewId = 0xB055;   ///< sentinel id for the boss view.
    static constexpr std::uint32_t kPlayerViewId = 0xF00D; ///< sentinel id for the player in SimEvents
                                                           ///< (enemy view ids are small indices 0..N).

    Simulation(int runSeed, WorldCollision *world);

    Simulation(const Simulation &) = delete;
    Simulation &operator=(const Simulation &) = delete;
    Simulation(Simulation &&) = delete;
    Simulation &operator=(Simulation &&) = delete;

    // --- setup (call before driving) ---------------------------------------
    /// Spawn an EnemyAI01 enemy (slice hp) in @p roomId; Activated immediately (asleep).
    void AddEnemy(const Game::EnemyDef &def, glm::vec2 spawn, int roomId, int seed);
    /// Spawn the single boss in @p roomId; Activated immediately (asleep).
    void SetBoss(float baseShootCd, glm::vec2 spawn, int maxHp, int roomId, int seed);
    /// Build (or rebuild, cold) the player weapon from a def. "Gun016" -> HeatMinigun.
    void EquipWeapon(const Game::WeaponDef &def, const std::string &weaponId, int seed);
    /// Build (or rebuild) the player's skill brain (A3: fixed c01). The skill is a
    /// SINGLE owned member, parallel to the weapon -- not an entity, not in any list,
    /// not via BrainFactory (a bespoke per-hero brain, no base class). @p skillCd /
    /// @p inSkillTime come from the CharacterDef stat sheet.
    void SetPlayerSkill(float skillCd, float inSkillTime);
    /// Seed the player's combat vitals (enemy bullets damage these; read back via PlayerStats).
    void SetPlayerStats(const Game::CombatStats &stats);

    // --- drive -------------------------------------------------------------
    /// Run FixedClock.Advance(dtMs) fixed Step()s with @p in held constant. Returns step count.
    int Advance(float dtMs, const WorldInputs &in);

    // --- views (poll after Advance) ----------------------------------------
    const std::vector<BulletState> &Bullets() const { return m_Bullets; }
    std::vector<EntityView> EnemyViews() const;
    bool HasBoss() const { return m_Boss != nullptr; }
    EntityView BossView() const;
    const Game::CombatStats &PlayerStats() const { return m_PlayerStats; }
    int PlayerShotsLastAdvance() const { return m_PlayerShotsLastAdvance; }
    /// The player's skill brain (A3), or empty if SetPlayerSkill was never called.
    /// Exposed for inspection/tests (InSkill / SkillReady / cooldown getters).
    const std::optional<Game::CharSkillC01> &PlayerSkill() const { return m_Skill; }
    /// Move out the queued anim/sfx events (empty this cycle; emission is deferred).
    std::vector<SimEvent> DrainEvents();

private:
    void Step();                              // one fixed tick (built across Tasks 3-7).
    void WakeByRoom();                         // awake = (roomId == m_Input.playerRoomId).
    void TickWeapon();                         // Task 3.
    void TickSkill();                          // A3: activate + cooldown + publish progress.
    void EmitAttackEvents();                   // A: per-controller fire -> AnimTrigger "attack".
    void MoveControllers();                    // Task 5/6.
    void DrainFireIntents();                   // Task 3.
    void IntegrateBullets();                   // Task 4.
    void ResolveHits();                        // Task 7.
    void Cull();                               // Task 5/7.
    static EntityView ViewOf(const EntityState &s, std::uint32_t id);

    int m_RunSeed;
    NullWorldCollision m_NullWorld;          ///< fallback; declared BEFORE m_World so the
                                             ///< ctor can point m_World at it safely.
    WorldCollision *m_World;                 ///< non-owning; never null (ctor uses m_NullWorld if null).

    FixedClock m_Clock;
    Scheduler m_Scheduler;
    FireSystem m_FireSystem;
    std::vector<FireIntent> m_FireIntents; ///< controllers push here; cleared each step.
    std::vector<BulletState> m_Bullets;
    std::uint32_t m_NextBulletId = 1;
    Game::RGRandom m_HitRng;

    std::vector<std::unique_ptr<EnemyController>> m_Enemies;
    std::unique_ptr<BossController> m_Boss;
    std::optional<WeaponController> m_Weapon;
    /// The player's skill brain (A3: fixed c01). A SINGLE owned member, parallel to
    /// m_Weapon -- not an entity, not in m_Enemies, not via BrainFactory.
    std::optional<Game::CharSkillC01> m_Skill;

    Game::CombatStats m_PlayerStats{};
    WorldInputs m_Input{};
    int m_PlayerShotsLastAdvance = 0;
    std::vector<SimEvent> m_Events;
};

} // namespace Game::Sim

#endif /* GAME_SIM_SIMULATION_HPP */
