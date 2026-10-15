#ifndef GAME_SIM_SKILLDASH_HPP
#define GAME_SIM_SKILLDASH_HPP

#include <cmath>

#include <glm/glm.hpp>

#include "sim/BodyMove.hpp"
#include "sim/WorldCollision.hpp"

namespace Game::Sim {

/// C02 ultimate forward-dash distance (px), owner-applied. PLACEHOLDER, NOT
/// RE-recovered: C02Controller__RoleSkillEnd (game_full.c:156134) applies a
/// GetForce(dir, mag) velocity impulse + a -0.15 friction carry (CharSkillC02.hpp
/// :51-62); the `mag` operand was not captured in the decomp, so this fixed 80px
/// step is a hand-tuned owner stand-in (debt, like kBoxHp / kSecondHandOffsetPx).
/// P3.0 audit: a PORT CHOICE (T-56) in px, deliberately NOT converted (the real distance is an
/// unrecovered GetForce magnitude in units/s, capped at 30).
/// SINGLE source: both the GameScene owner and the RealBodyDriver test mirror dash
/// by this exact constant -- there is no second hand-copied value.
inline constexpr float kSkillDashPx = 80.0F;

/// Apply a dash displacement to @p body along @p aimDir of length @p dist, gated by @p wc like walking
/// (the air-wall lesson: an owner displacement must be wall-gated exactly like walking). In a world
/// with exact MoveBlocked (GameScene) the dash cannot tunnel through any blocker; in a world on the
/// default sampled MoveBlocked (test doubles) a shallow graze can slip through (see WorldCollision). P2.18 (PORT CHOICE T-62): the old version tested only the
/// 80 px END point per axis, so a 1-cell (32 px) wall between start and end was jumped. It now
/// (1) depenetrates the start (a body that begins overlapping a blocker can dash out) and
/// (2) moves with the swept, axis-sliding MoveBody in sub-steps of at most one body radius, flush
/// to contact: a dash toward a wall ends ON the near side of it, a dash along a wall slides.
/// FAITHFUL (source chain): the original dash calls GetForce (game_typed.c:714), which writes the
/// direction and inertia velocity (:43958) and hands it to Rigidbody2D.set_velocity (:51043); it is
/// not cancelled when a wall is near. INFERRED: Box2D then stops the body at the wall, so ending
/// flush against it is an approximation of that outcome (T-62), not a point-exact reproduction.
/// @p aimDir need not be unit-length (zero-length falls back to +x). This is the SINGLE shared
/// owner-side dash-apply: GameScene::ConsumeSimEvents and the RealBodyDriver test both call this
/// exact function (no duplicated implementation).
inline void ApplyDash(const WorldCollision &wc, glm::vec2 &body, glm::vec2 aimDir,
                      float dist, float radius) {
    const float len = std::sqrt(aimDir.x * aimDir.x + aimDir.y * aimDir.y);
    const glm::vec2 dir = len > 0.0F ? aimDir / len : glm::vec2{1.0F, 0.0F};
    body = wc.Depenetrate(body, radius);
    body = MoveBody(wc, body, dir * dist, radius, /*flushToContact=*/true);
}

} // namespace Game::Sim

#endif /* GAME_SIM_SKILLDASH_HPP */
