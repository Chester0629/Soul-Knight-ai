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

    /// The only normal (non-forced) writer of `deathSource`: a no-op once it is
    /// already Forced (immutable), otherwise records @p src as the latest
    /// damage-application source. KillAllHostiles bypasses this helper and assigns
    /// `deathSource = DeathSource::Forced` directly.
    void SetDeathSource(DeathSource src) {
        if (deathSource != DeathSource::Forced) {
            deathSource = src;
        }
    }
};

} // namespace Game::Sim

#endif /* GAME_SIM_ENTITYSTATE_HPP */
