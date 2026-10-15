#ifndef GAME_SIM_BULLETSTATE_HPP
#define GAME_SIM_BULLETSTATE_HPP

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "combat/Buff.hpp"
#include "sim/BulletShape.hpp"

namespace Game::Sim {

/// A logical bullet owned by the Simulation. GameScene mirrors each to a pooled
/// PTSD Bullet by stable id for rendering.
struct BulletState {
    std::uint32_t id = 0;
    glm::vec2 pos{0.0F, 0.0F};
    glm::vec2 vel{0.0F, 0.0F}; ///< pixels/second.
    float lifeMs = 0.0F;
    int damage = 0;
    int camp = 0; ///< 0 = player bullet, 1 = enemy bullet.
    /// Hit/wall radius (px) from the bullet prefab collider (BulletDef::collider, via
    /// FireIntent::radiusPx); 0 => the fallback Sim::kBulletRadius.
    float radiusPx = 0.0F;
    /// P3.0c: the bullet prefab this bullet was launched as (view sprite / hit effect id), its
    /// prefab collision shape and its spin (see BulletLaunch). Default = no prefab (legacy circle).
    BulletLaunch launch;
    /// P3.0c: heading in radians = the bullet root's rotation (local +x = travel direction):
    /// atan2(vel) at spawn (RGBullet.SetBulletVelocity: transform.right = dir), plus the spin
    /// accumulated since (launch.spinDegPerStep per fixed step).
    float angleRad = 0.0F;
    float repel = 0.0F;
    int critical = 0;
    bool canThrough = false;
    int pierce = 0;
    bool active = false;
    // P3.2: carried from the FireIntent that spawned this bullet (see
    // FireIntent's own doc comment) -- resolved once at EquipWeapon time so it
    // survives a mid-flight weapon swap.
    bool hasBuff = false;
    Game::Combat::BuffParams buffParams{};
    float buffProbability = 0.0F;
    // T-57 (swept collision) per-step scratch, written by Simulation::IntegrateBullets and
    // read by ResolveHits in the SAME fixed step; meaningless between steps. `pos` is the
    // FULL end-of-step position during ResolveHits (the wall contact point only replaces it
    // for a bullet that ResolveHits then consumes at that wall).
    glm::vec2 prevPos{0.0F, 0.0F}; ///< position at the START of the current step (sweep origin).
};

/// The ONE way a bullet's velocity changes after spawn: sets the velocity AND turns the bullet root
/// to it (FAITHFUL: RGBullet$$SetBulletVelocity DISASM 0x5b8f98 does `transform.right = dir` before
/// `rigid2d.velocity = right * speed`), so the drawn heading and the oriented collider can never lag
/// the direction of travel. A zero velocity keeps the previous heading.
inline void SetBulletVelocity(BulletState &b, glm::vec2 velocity) {
    b.vel = velocity;
    if (velocity.x != 0.0F || velocity.y != 0.0F) {
        b.angleRad = std::atan2(velocity.y, velocity.x);
    }
}

/// FAITHFUL: RGBullet$$ReboundBullet (0x5b9118): `transform.right = -transform.right` and the
/// velocity is negated, i.e. the bullet turns around and flies back along its path.
inline void ReboundBullet(BulletState &b) {
    SetBulletVelocity(b, -b.vel);
}

} // namespace Game::Sim

#endif /* GAME_SIM_BULLETSTATE_HPP */
