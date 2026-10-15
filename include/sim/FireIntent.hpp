#ifndef GAME_SIM_FIREINTENT_HPP
#define GAME_SIM_FIREINTENT_HPP

#include <glm/glm.hpp>

#include "combat/Buff.hpp"
#include "sim/BulletShape.hpp"

namespace Game::Sim {

/// Bullet-spawn shapes. Single + Fan are implemented in Plan 1; Burst/Charge/
/// Parabola are reserved for the guns wired in later cycles.
enum class FirePattern { Single, Fan, Burst, Charge, Parabola };

/// A brain's request to spawn bullets this tick. For Single the brain has already
/// baked any scatter into `dir` (so all RNG stays brain-side); for Fan, `dir` is
/// the centre and the FireSystem spreads `count` bullets over `spreadDeg`.
struct FireIntent {
    glm::vec2 origin{0.0F, 0.0F};
    glm::vec2 dir{1.0F, 0.0F}; ///< unit aim direction.
    FirePattern pattern = FirePattern::Single;
    int count = 1;             ///< number of bullets (Fan).
    float spreadDeg = 0.0F;    ///< total fan spread (Fan only).
    /// pixels/second. If sourced from a data bulletSpeed, the caller multiplies by
    /// Sim::kDataSpeedToPxPerSec (see SimConfig.hpp: 1 Unity unit = 32 px) -- raw data
    /// units are not px/s.
    float speedPxPerSec = 0.0F;
    /// milliseconds. If sourced from BulletDef::destroyTime (seconds), the caller
    /// multiplies by 1000.0F -- a missed conversion gives 1000x-too-short bullets.
    float lifeMs = 0.0F;
    /// Charge release scalar (B1-P3): for FirePattern::Charge the FireSystem scales
    /// the bullet speed by this ratio (the driver/charge-gun brain accrues it while
    /// the trigger is held). Default 1.0 leaves Single/Fan paths byte-identical
    /// (those branches never read it). >1 is a valid over-charge (no clamp).
    float chargeRatio = 1.0F;
    int damage = 0;
    float repel = 0.0F;
    int critical = 0;
    bool canThrough = false;
    int pierce = 0;
    int camp = 0;
    /// Bullet collision radius (px) from the bullet prefab's collider; 0 => fallback.
    float radiusPx = 0.0F;
    /// P3.0c: bullet prefab row + collision shape + spin (default = none -> radiusPx circle).
    BulletLaunch launch;
    // P3.2: the firing weapon's bullet buff (resolved once at EquipWeapon time
    // from BulletDef::buffId + buffs.json -> Combat::BuffParams, so it travels
    // with the bullet unchanged even if the player swaps weapons before this
    // bullet lands). hasBuff false => no buff (buffProbability 0 still applies on a crit or
    // for camp != 0, game_typed.c 86608-86645), byte-identical
    // to a bullet with no BuffTrigger.
    bool hasBuff = false;
    Game::Combat::BuffParams buffParams{};
    float buffProbability = 0.0F; ///< 0..1 chance to apply buffParams on a landed hit.
};

} // namespace Game::Sim

#endif /* GAME_SIM_FIREINTENT_HPP */
