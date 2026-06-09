#ifndef GAME_SIM_SIMMATH_HPP
#define GAME_SIM_SIMMATH_HPP

#include <cmath>

#include <glm/glm.hpp>

namespace Game::Sim {

/// Degrees -> radians.
inline constexpr float kDegToRad = 3.14159265358979F / 180.0F;

/// Rotate @p v by @p deg counter-clockwise (screen-math, +y up).
inline glm::vec2 RotateDeg(glm::vec2 v, float deg) {
    const float r = deg * kDegToRad;
    const float c = std::cos(r);
    const float s = std::sin(r);
    return glm::vec2{v.x * c - v.y * s, v.x * s + v.y * c};
}

/// Unit vector along @p v; returns {1,0} for the zero vector (never NaN).
inline glm::vec2 Normalize(glm::vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 0.0F ? v / len : glm::vec2{1.0F, 0.0F};
}

} // namespace Game::Sim

#endif /* GAME_SIM_SIMMATH_HPP */
