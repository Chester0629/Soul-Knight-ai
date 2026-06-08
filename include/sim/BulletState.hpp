#ifndef GAME_SIM_BULLETSTATE_HPP
#define GAME_SIM_BULLETSTATE_HPP

#include <cstdint>

#include <glm/glm.hpp>

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
};

} // namespace Game::Sim

#endif /* GAME_SIM_BULLETSTATE_HPP */
