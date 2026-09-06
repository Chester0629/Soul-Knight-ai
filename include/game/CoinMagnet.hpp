#ifndef GAME_COIN_MAGNET_HPP
#define GAME_COIN_MAGNET_HPP

#include <glm/glm.hpp>

#include "sim/SimConfig.hpp"

namespace Game {

/**
 * @struct CoinMagnet
 * @brief Pure motion model of a dropped coin / energy pickup -- the port of the
 *        RGCoin component's fields (RGCoin.cs: speed, range, friction,
 *        inertial_vel, force_direction, find_target). Engine-free, unit-tested.
 *
 * Two phases, as in the original:
 *   1. SCATTER: on spawn GetForce(direction, impulse) gives the coin an inertial
 *      velocity along `force_direction` that decays by `friction` (0.9) every
 *      fixed step (RGCoin ctor: friction = 0.9f, i.e. 0x3f666666).
 *   2. MAGNET: FindTarget marks the pickup as homing once the player is within
 *      `range` world units; it then moves straight at the player at `speed`
 *      units/s and is collected on contact (GetItem).
 * World units -> pixels use the same factor as every other data speed
 * (Sim::kDataSpeedToPxPerSec = 15 px per unit).
 */
struct CoinMagnet {
    /// FAITHFUL: RGCoin.friction @ RGCoin__ctor (0.9f), per fixed step (50 Hz).
    static constexpr float kFriction = 0.9F;
    static constexpr float kFixedStepSec = 0.02F;
    /// Contact radius (px): coin CircleCollider2D 0.44..0.56 units ~ 8 px + body.
    static constexpr float kCollectRadiusPx = 20.0F;

    glm::vec2 pos{0.0F, 0.0F};
    glm::vec2 forceDir{0.0F, 0.0F}; ///< RGCoin.force_direction (unit).
    float inertialPxPerSec = 0.0F;  ///< RGCoin.inertial_vel, in px/s.
    float speedPxPerSec = 0.0F;     ///< RGCoin.speed * 15.
    float rangePx = 0.0F;           ///< RGCoin.range * 15.
    bool findTarget = false;        ///< RGCoin.find_target: homing latched.
    float stepAccum = 0.0F;

    /// @p speedUnits / @p rangeUnits are the prefab's RGCoin.speed / RGCoin.range.
    static CoinMagnet Make(glm::vec2 at, float speedUnits, float rangeUnits) {
        CoinMagnet m;
        m.pos = at;
        m.speedPxPerSec = speedUnits * Sim::kDataSpeedToPxPerSec;
        m.rangePx = rangeUnits * Sim::kDataSpeedToPxPerSec;
        return m;
    }

    /// RGCoin.GetForce(direction, value): the spawn scatter impulse.
    void GetForce(glm::vec2 dir, float impulseUnitsPerSec) {
        const float len = glm::length(dir);
        forceDir = len > 0.0F ? dir / len : glm::vec2{0.0F, 0.0F};
        inertialPxPerSec = impulseUnitsPerSec * Sim::kDataSpeedToPxPerSec;
    }

    /// Advance by @p dtMs (fixed 20 ms sub-steps). @return true when the pickup
    /// touched the player this step (the caller collects it).
    bool Step(float dtMs, glm::vec2 playerPos) {
        stepAccum += dtMs / 1000.0F;
        bool collected = false;
        while (stepAccum >= kFixedStepSec) {
            stepAccum -= kFixedStepSec;
            const glm::vec2 toPlayer = playerPos - pos;
            const float dist = glm::length(toPlayer);
            if (!findTarget && dist <= rangePx) {
                findTarget = true; // RGCoin.FindTarget
            }
            if (findTarget) {
                if (dist <= kCollectRadiusPx) {
                    collected = true;
                    break;
                }
                pos += (toPlayer / dist) * speedPxPerSec * kFixedStepSec;
            } else if (inertialPxPerSec > 0.5F) {
                pos += forceDir * inertialPxPerSec * kFixedStepSec;
                inertialPxPerSec *= kFriction;
            }
        }
        return collected;
    }
};

} // namespace Game

#endif /* GAME_COIN_MAGNET_HPP */
