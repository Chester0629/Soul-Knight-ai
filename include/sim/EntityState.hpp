#ifndef GAME_SIM_ENTITYSTATE_HPP
#define GAME_SIM_ENTITYSTATE_HPP

#include <glm/glm.hpp>

#include "combat/Buff.hpp"
#include "combat/CombatStats.hpp"

namespace Game::Sim {

/// item 2 (R19 kill-attribution fix): WHY this entity is currently dying, tracked
/// as the source of the LAST damage-application event applied to it (None while
/// alive/untouched) -- replaces the old plain-bool EntityState::lastHitByPlayer
/// (R18/item 5), which could only say "was the player" and had no way to lock
/// itself once a FORCED death (KillAllHostiles/SK_FORCE_KILL) happened. That gap
/// mattered because KillAllHostiles zeroes hp immediately but does not mark the
/// entity `dead` (only Simulation::Cull() does, the next time it runs) -- so a
/// player-sourced DoT tick already in flight, or a stray player hit, landing in
/// the window between the forced kill and the next Cull() would silently flip
/// attribution back to the player. EntityState::SetDeathSource() is the ONLY
/// writer every normal (non-forced) damage-application site should use: once
/// `deathSource` is Forced it is IMMUTABLE (SetDeathSource becomes a no-op) for
/// the rest of this entity's life; KillAllHostiles instead assigns `deathSource`
/// directly (a forced kill always wins immediately, overwriting whatever came
/// before). Cull() fires ICharSkill::OnEnemyKilled iff deathSource is
/// PlayerBullet/PlayerCut/PlayerDot -- None, Other, and Forced never do.
enum class DeathSource {
    None,         ///< no damage applied yet, or nothing dying.
    PlayerBullet, ///< a player bullet hit (Simulation::ResolveHits).
    PlayerCut,    ///< C04's melee sword cut (Simulation::ApplyMeleeCut).
    PlayerDot,    ///< a player-sourced buff's DoT tick (Simulation::TickBuffs).
    Other,        ///< a non-player-sourced buff's DoT tick (Simulation::TickBuffs).
    Forced,       ///< KillAllHostiles/SK_FORCE_KILL -- immutable once set.
};

/// Gameplay state a controller owns; the PTSD entity is a view onto it.
struct EntityState {
    glm::vec2 pos{0.0F, 0.0F};
    glm::vec2 vel{0.0F, 0.0F};
    glm::vec2 facing{1.0F, 0.0F};
    CombatStats stats{};
    bool awake = false;
    bool dead = false;
    bool kinematic = false;
    int roomId = -1;

    // --- P3.1: buff engine (Game::Combat::BuffList; see Simulation::TickBuffs) ---
    Combat::BuffList buffs{};
    /// This step's combined movement-speed multiplier from active buffs (1.0 = none);
    /// MoveControllers() multiplies the controller's velocity by this every step.
    float buffSpeedMul = 1.0F;
    /// True iff buffSpeedMul is exactly 0 this step (Ice/Electric freeze).
    bool buffFrozen = false;
    /// The armor amount granted by the currently-active Armor buff (0 if none), so
    /// Simulation::TickBuffs can revert exactly this much when the buff ends.
    int buffArmorGranted = 0;

    /// item 2 (R19 kill-attribution fix, was item 5's `lastHitByPlayer` bool). See
    /// the DeathSource enum doc above. Irrelevant once dead (never read again).
    DeathSource deathSource = DeathSource::None;

    /// item 2 (R20 fix): true once `deathSource` has been LOCKED by the first
    /// damage-application call that left `stats.hp <= 0` this life. Death
    /// attribution must belong to whichever hit was actually lethal, not to
    /// whichever hit happened to land last in Simulation::Step's fixed order
    /// (melee cut -> DoT -> bullets -> Cull). Without this flag a non-player DoT
    /// tick that already killed the enemy could be re-attributed to a player
    /// bullet landing later the SAME step (false OnEnemyKilled), and symmetrically
    /// a lethal player cut could be overwritten to Other by a later non-player DoT
    /// tick (missed OnEnemyKilled). Forced (KillAllHostiles/SK_FORCE_KILL) is
    /// exempt from this lock the same way it is exempt from SetDeathSource below:
    /// it is assigned directly to `deathSource` (never through SetDeathSource), so
    /// it always overwrites regardless of `deathLocked`, and the `deathSource ==
    /// Forced` check below then rejects every subsequent SetDeathSource call
    /// unconditionally -- Forced always keeps priority.
    bool deathLocked = false;

    /// The only normal (non-forced) writer of `deathSource`: a no-op once
    /// `deathLocked` is set or `deathSource` is already Forced (both immutable),
    /// otherwise records @p src as the latest damage-application source and, if
    /// this application just brought `stats.hp` to <= 0, LOCKS it as the death's
    /// attribution (first lethal hit wins, not the last hit of the step). Callers
    /// must invoke this immediately after applying the damage that may have been
    /// lethal (stats.hp already updated) so the hp<=0 check below observes it.
    /// KillAllHostiles bypasses this helper and assigns
    /// `deathSource = DeathSource::Forced` directly.
    void SetDeathSource(DeathSource src) {
        if (deathLocked || deathSource == DeathSource::Forced) {
            return;
        }
        deathSource = src;
        if (stats.IsDead()) {
            deathLocked = true;
        }
    }
};

} // namespace Game::Sim

#endif /* GAME_SIM_ENTITYSTATE_HPP */
