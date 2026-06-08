#ifndef GAME_SIM_WORLDCOLLISION_HPP
#define GAME_SIM_WORLDCOLLISION_HPP

#include <glm/glm.hpp>

namespace Game::Sim {

/// Wall-blocking query the Simulation calls before committing a move. GameScene
/// implements it over the room AABBs + sealed doors; tests use NullWorldCollision.
class WorldCollision {
public:
    virtual ~WorldCollision() = default;
    virtual bool Blocks(glm::vec2 pos, float radius) const = 0;
};

/// A WorldCollision that never blocks (open arena), for headless tests.
class NullWorldCollision : public WorldCollision {
public:
    bool Blocks(glm::vec2 /*pos*/, float /*radius*/) const override { return false; }
};

} // namespace Game::Sim

#endif /* GAME_SIM_WORLDCOLLISION_HPP */
