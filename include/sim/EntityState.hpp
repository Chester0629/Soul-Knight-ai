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

    /// P3.3.b: true once a player bullet or melee cut has applied damage to this
    /// entity (ResolveHits / Simulation::ApplyMeleeCut set it). Cull() reads it to
    /// attribute a death to the player -> ICharSkill::OnEnemyKilled (C04's combo).
    /// Never cleared (an entity dies at most once); irrelevant once dead.
    bool lastHitByPlayer = false;
};

} // namespace Game::Sim

#endif /* GAME_SIM_ENTITYSTATE_HPP */
