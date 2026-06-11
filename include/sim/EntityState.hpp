#ifndef GAME_SIM_ENTITYSTATE_HPP
#define GAME_SIM_ENTITYSTATE_HPP

#include <glm/glm.hpp>

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
};

} // namespace Game::Sim

#endif /* GAME_SIM_ENTITYSTATE_HPP */
