#ifndef GAME_SIM_WORLDINPUTS_HPP
#define GAME_SIM_WORLDINPUTS_HPP

#include <glm/glm.hpp>

namespace Game::Sim {

/// Per-frame input the shell feeds the Simulation. Held constant across the N fixed
/// steps of one Advance(). The player is an INPUT, not a sim-integrated entity:
/// movement + energy stay shell-side (Plan 4b).
struct WorldInputs {
    glm::vec2 playerPos{0.0F, 0.0F};
    glm::vec2 aimDir{1.0F, 0.0F}; ///< unit aim (shell normalizes).
    bool playerAlive = true;
    bool firing = false;
    int playerRoomId = -1; ///< wakes controllers whose roomId matches.
};

} // namespace Game::Sim

#endif /* GAME_SIM_WORLDINPUTS_HPP */
