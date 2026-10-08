#ifndef GAME_SIM_BULLETSTATE_HPP
#define GAME_SIM_BULLETSTATE_HPP

#include <cstdint>

#include <glm/glm.hpp>

#include "combat/Buff.hpp"

namespace Game::Sim {

/// A logical bullet owned by the Simulation. GameScene mirrors each to a pooled
/// PTSD Bullet by stable id for rendering.
struct BulletState {
    std::uint32_t id = 0;
    glm::vec2 pos{0.0F, 0.0F};
    glm::vec2 vel{0.0F, 0.0F}; ///< pixels/second.
    float lifeMs = 0.0F;
    int damage = 0;
    int camp = 0; ///< 0 = player bullet, 1 = enemy bullet.
    /// Hit/wall radius (px) from the bullet prefab collider (BulletDef::collider, via
    /// FireIntent::radiusPx); 0 => the fallback Sim::kBulletRadius.
    float radiusPx = 0.0F;
    float repel = 0.0F;
    int critical = 0;
    bool canThrough = false;
    int pierce = 0;
    bool active = false;
    // P3.2: carried from the FireIntent that spawned this bullet (see
    // FireIntent's own doc comment) -- resolved once at EquipWeapon time so it
    // survives a mid-flight weapon swap.
    bool hasBuff = false;
    Game::Combat::BuffParams buffParams{};
    float buffProbability = 0.0F;
    // T-57 (swept collision) per-step scratch, written by Simulation::IntegrateBullets and
    // read by ResolveHits in the SAME fixed step; meaningless between steps.
    glm::vec2 prevPos{0.0F, 0.0F}; ///< position at the START of the current step (sweep origin).
    bool wallStop = false;         ///< the step's path hit a wall: pos = contact point.
};

} // namespace Game::Sim

#endif /* GAME_SIM_BULLETSTATE_HPP */
