#ifndef GAME_COMBAT_SKILL_WORLD_HPP
#define GAME_COMBAT_SKILL_WORLD_HPP

#include <cstdint>
#include <functional>

#include <glm/glm.hpp>

namespace Game {

/**
 * @file SkillWorld.hpp
 * @brief Engine-free world vocabulary shared by the P3.3.c skill brains (C03 thunder
 *        chain, C07 bat swarm, C11 holy light). Everything here is expressed in Unity
 *        WORLD UNITS -- the unit the decompiled Physics2D radii / Rigidbody2D
 *        velocities use -- so the brains stay free of the sim's pixel scale; the
 *        Sim-side adapters convert with Sim::kWorldUnitPx (T-40).
 */

/// One hostile a skill effect can target: the sim's stable entity id + its position.
struct SkillTarget {
    std::uint32_t id = 0;
    glm::vec2 pos{0.0F, 0.0F};
};

/// Wall line-of-sight query (Physics2D.Linecast on layer "Wall"): true = blocked.
/// An empty function means nothing blocks.
using SkillLineBlockedFn = std::function<bool(glm::vec2, glm::vec2)>;

} // namespace Game

#endif /* GAME_COMBAT_SKILL_WORLD_HPP */
