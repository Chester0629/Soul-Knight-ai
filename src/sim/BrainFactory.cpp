#include "sim/BrainFactory.hpp"

namespace Game::Sim {

EnemyController BrainFactory::MakeEnemy(const Game::EnemyDef &def, glm::vec2 spawn,
                                        int seed) {
    EnemyController::Params p;
    p.speed = 60.0F;                      // slice default (EnemyDef has no per-enemy speed)
    p.speedRate = 0.0F;                   // slice default (EnemyDef has no speed_rate field)
    p.friction = def.friction;
    p.scoutRateSeconds = def.scoutRate;
    p.shootCdSeconds = def.shootCd;
    p.kinematic = def.kinematic != 0;
    return EnemyController(p, spawn, seed); // prvalue -> C++17 guaranteed copy elision
}

} // namespace Game::Sim
