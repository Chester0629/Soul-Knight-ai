#ifndef GAME_SIM_FIREINTENT_HPP
#define GAME_SIM_FIREINTENT_HPP

#include <glm/glm.hpp>

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
    /// Sim::kDataSpeedToPxPerSec (see SimConfig.hpp) -- raw data units are not px/s.
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
};

} // namespace Game::Sim

#endif /* GAME_SIM_FIREINTENT_HPP */
