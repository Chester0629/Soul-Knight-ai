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
};

} // namespace Game::Sim

#endif /* GAME_SIM_BULLETSTATE_HPP */
