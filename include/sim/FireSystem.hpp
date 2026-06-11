#ifndef GAME_SIM_FIRESYSTEM_HPP
#define GAME_SIM_FIRESYSTEM_HPP

#include <cstdint>
#include <vector>

#include "sim/BulletState.hpp"
#include "sim/FireIntent.hpp"

namespace Game::Sim {

/// Expands brain-issued FireIntents into BulletStates. Pure geometry: it takes
/// NO RNG draws (the brain already drew any scatter and baked it into the intent).
class FireSystem {
public:
    /// Append the bullets for @p intent to @p out, assigning ids from @p nextId
    /// (advanced per bullet). Implements Single + Fan; other patterns currently
    /// emit a single bullet along intent.dir (reserved for later cycles).
    /// @pre intent.dir must be a unit vector -- bullet velocity is dir*speed, so a
    ///      non-unit dir produces the wrong speed. A Fan with count <= 0 emits
    ///      nothing; count == 1 emits one shot along dir.
    void Expand(const FireIntent &intent, std::uint32_t &nextId,
                std::vector<BulletState> &out) const;
};

} // namespace Game::Sim

#endif /* GAME_SIM_FIRESYSTEM_HPP */
