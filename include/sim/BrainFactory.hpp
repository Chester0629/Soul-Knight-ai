#ifndef GAME_SIM_BRAINFACTORY_HPP
#define GAME_SIM_BRAINFACTORY_HPP

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "sim/EnemyController.hpp"

namespace Game::Sim {

/// Builds sim controllers from data definitions. The single place that maps a
/// content def to its controller config; the extension point for the rest of the
/// roster in later cycles.
class BrainFactory {
public:
    /// Build an EnemyController from an EnemyDef at @p spawn, seeded with @p seed.
    static EnemyController MakeEnemy(const Game::EnemyDef &def, glm::vec2 spawn, int seed);
};

} // namespace Game::Sim

#endif /* GAME_SIM_BRAINFACTORY_HPP */
