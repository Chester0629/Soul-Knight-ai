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
#include "combat/Damage.hpp"
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

    /// TEST ACCESSOR (read-only): number of hostiles whose death Cull() credited to the
    /// player (DeathSource PlayerBullet/PlayerCut/PlayerDot/PlayerSkill -- the same
    /// condition that fires ICharSkill::OnEnemyKilled). Forced/Other/None deaths never
    /// count, so a wrong DeathSource on a skill path flips this.
    int PlayerKillCount() const { return m_PlayerKillCount; }

    /// TEST ACCESSOR (read-only): the deathSource of the most recent hostile Cull() killed
    /// (DeathSource::None before any death).
    DeathSource LastDeathSourceForTest() const { return m_LastDeathSource; }

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
    /// player_template today -- per-char overrides pending). @p atk (P3.3.b) is the
    /// hero's base role_attribute.atk (CharacterDef::atk), fed to skills whose effect
    /// needs it (C04's sword cut: atk + 3/7); default 0 leaves every caller that does
    /// not pass it byte-identical for heroes with no atk-driven effect. The skill is
    /// a SINGLE owned member parallel to the weapon.
    ///
    /// P3.3.c: @p def (optional) is the resolved CharacterDef; heroes whose ultimate
    /// reads effect-prefab data (C05 bottle variants, C07 BulletBat.damage via
    /// CharacterDef::skillRefs/skillParams) take it from there instead of a C++
    /// literal. Null -> the decompile ctor defaults.
    void SetPlayerSkill(const std::string &charId, float skillCd, float inSkillTime,
                        int atk = 0, const Game::CharacterDef *def = nullptr);
    /// Seed the player's combat vitals (enemy bullets damage these; read back via PlayerStats).
    void SetPlayerStats(const Game::CombatStats &stats);

    /// P3.1: apply (or refresh) a status-effect buff to one entity. @p entityId uses
    /// the same scheme as EntityView::id / SimEvent::entityId: kPlayerViewId,
    /// kBossViewId, or an enemy index 0..N. A no-op if entityId names a boss/enemy
    /// that does not currently exist. Reapplying the same BuffKind refreshes its
    /// remaining duration (does not stack). Bullet-trigger wiring (auto-applying a
    /// buff on hit) is P3.2 -- this is the manual/test-hook entry point only.
    /// @p fromPlayer (item 5, kill attribution) records whether this application is
    /// player-sourced, so a later DoT tick from it can correctly mark
    /// EntityState::deathSource (DeathSource::PlayerDot); defaults to false (the manual/test-hook use of
    /// this entry point is not, by itself, a player action).
    void ApplyBuff(std::uint32_t entityId, const Game::Combat::BuffParams &params,
                   bool fromPlayer = false);
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
    /// P3.3.c: energy a skill restored during the last Advance() (C07 BatBack's
    /// RoleAttributePlayer.RestoreEnergy). Energy is scene-owned (the sim never
    /// mutates it), so the shell adds this to its own stats, like it spends
    /// PlayerShotsLastAdvance(). Reset at the top of every Advance(). PORT CHOICE (T-46):
    /// the sim never owns energy, so a skill's RestoreEnergy is reported, not applied.
    int PlayerEnergyGrantedLastAdvance() const { return m_PlayerEnergyGrantedLastAdvance; }
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
    /// P3.3.b: route an incoming player hit through ICharSkill::OnPlayerHurt first
    /// (C08's shield absorb); apply the (possibly replaced) damage via
    /// Combat::ApplyToPlayer otherwise. @p hr.finalDamage is read/overwritten.
    void ApplyDamageToPlayer(Game::Combat::HitResult hr);
    /// P3.3.b: C04's melee sword cut -- deals @p damage to every live enemy/boss
    /// within kMeleeCutReachPx of the player and inside the kMeleeCutHalfAngleDeg
    /// forward arc of the current aim direction (see SimConfig.hpp T-36). Marks
    /// EntityState::deathSource (DeathSource::PlayerCut) on every entity hit (Cull's kill-attribution) and emits a
    /// "hurt" AnimTrigger per landed cut, same shape as a bullet hit.
    void ApplyMeleeCut(int damage);
    /// P3.3.c: build the ICharSkill world snapshot (live + awake hostiles, player
    /// vitals, wall line-of-sight query) handed to TickEffects.
    ICharSkill::SkillWorldView BuildSkillWorld() const;
    /// P3.3.c: apply a skill's world-effect output -- skill damage on hostiles
    /// (DeathSource::PlayerSkill), player heal / energy, the melee cut (T-36 reach)
    /// and the "skill_effect:<cue>" log events.
    void ApplySkillEffects(const ICharSkill::SkillEffectOut &out);
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
    /// P3.3.b: the hero's base role_attribute.atk (CharacterDef::atk), fed to
    /// ICharSkill::RoleAtkMelee (C04's cut damage = atk + 3/7). Set by
    /// SetPlayerSkill; 0 for callers that omit it (no atk-driven skill effect).
    int m_PlayerAtk = 0;
    /// R18/item-1 (+ R19 zero-substep fix, see Advance()): the raw WorldInputs::skill
    /// level observed by the PREVIOUS Advance() call, updated on EVERY Advance()
    /// regardless of how many fixed Step()s that call actually ran. WorldInputs::skill
    /// is a raw held-key level from the shell (GameScene passes IsKeyDown straight
    /// through -- NOT edge-detected there); this is compared against the new level at
    /// the TOP of Advance() to detect a false->true press-edge at CALL granularity
    /// (not fixed-step granularity -- see m_SkillPressPending), so a press is never
    /// missed even when its Advance() call happens to run zero fixed Step()s.
    bool m_SkillHeldPrev = false;
    /// R19/item-1 fix: the skill button's press-edge latch. Set in Advance() (see
    /// above) the instant a false->true edge on the raw input is observed, and held
    /// SET until a fixed Step()'s TickSkill() actually consumes it (calls
    /// TryTrigger() exactly once and clears it). This decouples "a press happened"
    /// from "a fixed Step() ran to notice it": IsKeyDown (PTSD/src/Util/Input.cpp) is
    /// a single-frame pulse, and Simulation::Advance(dtMs) can run ZERO fixed
    /// sub-steps for a short dt (m_Clock accumulates dtMs and only emits a Step()
    /// once the accumulator crosses kFixedStepMs) -- with edge detection living
    /// in TickSkill() alone, a press occurring during such a zero-step Advance()
    /// would be silently overwritten by the NEXT Advance() call before any Step()
    /// ever observed skill=true, dropping the press entirely. Latching the edge here
    /// (at Advance()-call granularity) instead of inside TickSkill() (fixed-step
    /// granularity) fixes that while preserving the existing "held across many
    /// sub-steps/Advance() calls counts once" behaviour, since the latch is only
    /// ever (re)armed on a genuine false->true transition of the raw input.
    bool m_SkillPressPending = false;

    Game::CombatStats m_PlayerStats{};
    // --- P3.1: player buff state (the sim does not own the player's position, so
    // GameScene polls PlayerFrozen()/PlayerSpeedMul() to gate its own movement). ---
    Game::Combat::BuffList m_PlayerBuffs;
    float m_PlayerSpeedMul = 1.0F;
    bool m_PlayerFrozen = false;
    int m_PlayerArmorGranted = 0; ///< so the Armor buff's end can revert exactly this much.
    WorldInputs m_Input{};
    int m_PlayerShotsLastAdvance = 0;
    int m_PlayerEnergyGrantedLastAdvance = 0; ///< P3.3.c: skill energy restore this Advance().
    std::vector<SimEvent> m_Events;
    int m_PlayerKillCount = 0;                      ///< Cull() deaths credited to the player.
    DeathSource m_LastDeathSource = DeathSource::None; ///< most recent Cull() death's source.
};

} // namespace Game::Sim

#endif /* GAME_SIM_SIMULATION_HPP */
