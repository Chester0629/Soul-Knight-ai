#ifndef GAME_SIM_SIMULATION_HPP
#define GAME_SIM_SIMULATION_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "combat/Buff.hpp"
#include "combat/CombatStats.hpp"
#include "data/GameData.hpp"
#include "data/RGRandom.hpp"
#include "sim/BossController.hpp"
#include "sim/BulletState.hpp"
#include "sim/EnemyController.hpp"
#include "sim/FireIntent.hpp"
#include "sim/FireSystem.hpp"
#include "sim/FixedClock.hpp"
#include "sim/ICharSkill.hpp"
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

    /// TEST HOOK (SK_FORCE_KILL): zero every live hostile's hp so the next step's
    /// Cull emits their death events -- drives the reward / kill-count paths
    /// headlessly. Not game logic; never called in normal play.
    void KillAllHostiles();

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
    void SetBoss(float baseShootCd, glm::vec2 spawn, int maxHp, int roomId, int seed,
                 const std::string &bossId = "BossAI01");
    /// Build (or rebuild, cold) the player weapon from a def. "Gun016" -> HeatMinigun.
    /// @p data (P3.2), when non-null, resolves the weapon's bullet's buff-application
    /// trigger (BrainFactory::MakeWeapon) so ResolveHits() auto-applies it on a landed
    /// hit; null (the default) keeps every existing call site byte-identical.
    void EquipWeapon(const Game::WeaponDef &def, const std::string &weaponId, int seed,
                     const Game::GameData *data = nullptr);
    /// Build (or rebuild) the player's skill brain via (d) dispatch (B1-P4a). @p charId
    /// ("c01".."c13") selects the per-hero adapter (MakeCharSkill); unknown -> c01.
    /// @p skillCd / @p inSkillTime come from the CharacterDef stat sheet (one shared
    /// player_template today -- per-char overrides pending). The skill is a SINGLE
    /// owned member parallel to the weapon.
    void SetPlayerSkill(const std::string &charId, float skillCd, float inSkillTime);
    /// Seed the player's combat vitals (enemy bullets damage these; read back via PlayerStats).
    void SetPlayerStats(const Game::CombatStats &stats);

    /// P3.1: apply (or refresh) a status-effect buff to one entity. @p entityId uses
    /// the same scheme as EntityView::id / SimEvent::entityId: kPlayerViewId,
    /// kBossViewId, or an enemy index 0..N. A no-op if entityId names a boss/enemy
    /// that does not currently exist. Reapplying the same BuffKind refreshes its
    /// remaining duration (does not stack). Bullet-trigger wiring (auto-applying a
    /// buff on hit) is P3.2 -- this is the manual/test-hook entry point only.
    void ApplyBuff(std::uint32_t entityId, const Game::Combat::BuffParams &params);
    /// True iff the PLAYER is currently frozen (Ice/Electric): the shell (GameScene
    /// owns player movement, not the sim) must gate its own movement on this.
    bool PlayerFrozen() const { return m_PlayerFrozen; }
    /// The PLAYER's combined movement-speed multiplier this step (1.0 = no buff).
    float PlayerSpeedMul() const { return m_PlayerSpeedMul; }

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
    /// The player's skill brain (the (d) ICharSkill), or nullptr if SetPlayerSkill was
    /// never called. Exposed for inspection/tests (InSkill / SkillReady / cooldown).
    const ICharSkill *PlayerSkill() const { return m_Skill.get(); }
    /// Move out the queued anim/sfx events (empty this cycle; emission is deferred).
    std::vector<SimEvent> DrainEvents();

    /// P3.2 DECISION RULE, FAITHFUL @ BuffEffectTrigger$$TriggerWith
    /// (game_typed.c:86608-86645, disasm @ 00ae97dc). Exposed as a public static
    /// so every branch can be unit-tested directly (RollBulletBuff is private and
    /// only reachable through a landed hit).
    ///
    /// The decompile's `probability` is a 0..100 PERCENT; pass it on that scale.
    /// The float-status-word bit test at the top of TriggerWith selects the FIRST
    /// branch when `probability < 0 || probability == 0 || isnan(probability)` --
    /// i.e. exactly `!(probability > 0)`:
    ///
    ///   probability <= 0 (or NaN):  if (!isCritic) { if (bullet.camp == 0) return; }
    ///                               -> apply iff (isCritic || camp != 0);
    ///                                  skip ONLY when it is not a crit AND camp == 0.
    ///   probability >  0:           roll = UnityEngine.Random.Range(0, 100);
    ///                               if (probability <= roll) return;
    ///                               -> apply iff (probability > roll). Range(0,100)
    ///                                  is max-EXCLUSIVE (RGRandom::Range(int,int)),
    ///                                  so roll is 0..99 and probability >= 100
    ///                                  always applies.
    ///
    /// @param probability 0..100 percent (NOT a 0..1 fraction).
    /// @param isCritic    this hit's resolved crit flag (Combat::HitResult::isCrit).
    /// @param camp        the BULLET's camp (BulletState::camp; 0 = player bullet).
    /// @param rng         drawn from ONLY on the probability > 0 branch, faithfully.
    static bool ShouldApplyBulletBuff(float probability, bool isCritic, int camp,
                                      Game::RGRandom &rng);

private:
    void Step();                              // one fixed tick (built across Tasks 3-7).
    void WakeByRoom();                         // awake = (roomId == m_Input.playerRoomId).
    void TickWeapon();                         // Task 3.
    void TickSkill();                          // A3: activate + cooldown + publish progress.
    void EmitAttackEvents();                   // A: per-controller fire -> AnimTrigger "attack".
    void TickBuffs();                          // P3.1: advance every BuffList, apply DoT.
    void MoveControllers();                    // Task 5/6.
    void DrainFireIntents();                   // Task 3.
    void IntegrateBullets();                   // Task 4.
    void ResolveHits();                        // Task 7.
    void Cull();                               // Task 5/7.
    /// P3.2: if @p b carries a buff (hasBuff), decide it via ShouldApplyBulletBuff()
    /// (@p isCritic is THIS hit's resolved crit flag, Combat::HitResult::isCrit;
    /// the camp comes from b.camp) and, on success, ApplyBuff(targetId,
    /// b.buffParams). No-op when !b.hasBuff -- and no RNG is drawn in that case.
    void RollBulletBuff(const BulletState &b, std::uint32_t targetId, bool isCritic);
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
    /// The player's skill brain via (d) dispatch (B1-P4a). A SINGLE owned member,
    /// parallel to m_Weapon -- charId selects the per-hero adapter (MakeCharSkill).
    std::unique_ptr<ICharSkill> m_Skill;

    Game::CombatStats m_PlayerStats{};
    // --- P3.1: player buff state (the sim does not own the player's position, so
    // GameScene polls PlayerFrozen()/PlayerSpeedMul() to gate its own movement). ---
    Game::Combat::BuffList m_PlayerBuffs;
    float m_PlayerSpeedMul = 1.0F;
    bool m_PlayerFrozen = false;
    int m_PlayerArmorGranted = 0; ///< so the Armor buff's end can revert exactly this much.
    WorldInputs m_Input{};
    int m_PlayerShotsLastAdvance = 0;
    std::vector<SimEvent> m_Events;
};

} // namespace Game::Sim

#endif /* GAME_SIM_SIMULATION_HPP */
