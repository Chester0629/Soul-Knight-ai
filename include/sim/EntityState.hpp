#ifndef GAME_SIM_ENTITYSTATE_HPP
#define GAME_SIM_ENTITYSTATE_HPP

#include <glm/glm.hpp>

#include "combat/Buff.hpp"
#include "combat/CombatStats.hpp"

namespace Game::Sim {

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

    /// item 5 (R18/Codex block): whether the LAST damage source applied to this
    /// entity was the player -- NOT "was ever hit by the player" (the P3.3.b
    /// original semantics). Set true by a player bullet hit (ResolveHits), a C04
    /// melee cut (Simulation::ApplyMeleeCut), or a player-sourced buff's DoT tick
    /// (TickBuffs, ActiveBuff::fromPlayer); set false by a non-player-sourced DoT
    /// tick (TickBuffs) and unconditionally by a forced kill (KillAllHostiles /
    /// SK_FORCE_KILL) so that a stale true from an earlier player hit cannot
    /// misattribute a forced death. Cull() reads it (immediately before Kill(), so
    /// a forced-kill's explicit clear above always wins) to decide whether to
    /// attribute this death to the player -> ICharSkill::OnEnemyKilled (C04's
    /// combo). Irrelevant once dead (never read again).
    bool lastHitByPlayer = false;
};

} // namespace Game::Sim

#endif /* GAME_SIM_ENTITYSTATE_HPP */
