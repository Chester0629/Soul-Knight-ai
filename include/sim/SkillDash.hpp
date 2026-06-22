#ifndef GAME_SIM_SKILLDASH_HPP
#define GAME_SIM_SKILLDASH_HPP

#include <cmath>

#include <glm/glm.hpp>

#include "sim/WorldCollision.hpp"

namespace Game::Sim {

/// C02 ultimate forward-dash distance (px), owner-applied. PLACEHOLDER, NOT
/// RE-recovered: C02Controller__RoleSkillEnd (game_full.c:156134) applies a
/// GetForce(dir, mag) velocity impulse + a -0.15 friction carry (CharSkillC02.hpp
/// :51-62); the `mag` operand was not captured in the decomp, so this fixed 80px
/// step is a hand-tuned owner stand-in (debt, like kBoxHp / kSecondHandOffsetPx).
/// SINGLE source: both the GameScene owner and the RealBodyDriver test mirror dash
/// by this exact constant -- there is no second hand-copied value.
inline constexpr float kSkillDashPx = 80.0F;

/// Apply a dash displacement to @p body along @p aimDir of length @p dist, gated by
/// @p wc so the dash can NEVER tunnel through a wall (the air-wall lesson: an owner
/// displacement must be wall-gated exactly like walking): axis-separated, each axis
/// committed only if the destination circle (centre, @p radius) does not Block.
/// @p aimDir need not be unit-length (zero-length falls back to +x). This is the
/// SINGLE shared owner-side dash-apply: GameScene::ConsumeSimEvents and the
/// RealBodyDriver test both call this exact function (no duplicated implementation).
inline void ApplyDash(const WorldCollision &wc, glm::vec2 &body, glm::vec2 aimDir,
                      float dist, float radius) {
    const float len = std::sqrt(aimDir.x * aimDir.x + aimDir.y * aimDir.y);
    const glm::vec2 dir = len > 0.0F ? aimDir / len : glm::vec2{1.0F, 0.0F};
    const glm::vec2 step = dir * dist;
    // Axis-separated, collision-gated -- the SAME shape as the walk slide so a dash
    // toward a wall is refused (clamped), never teleporting the body through it.
    if (!wc.Blocks(glm::vec2{body.x + step.x, body.y}, radius)) {
        body.x += step.x;
    }
    if (!wc.Blocks(glm::vec2{body.x, body.y + step.y}, radius)) {
        body.y += step.y;
    }
}

} // namespace Game::Sim

#endif /* GAME_SIM_SKILLDASH_HPP */
