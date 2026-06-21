#ifndef GAME_SIM_WEAPONCONTROLLER_HPP
#define GAME_SIM_WEAPONCONTROLLER_HPP

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "combat/Gun001.hpp"
#include "combat/Gun016.hpp"
#include "sim/FireIntent.hpp"
#include "sim/IWeaponBrain.hpp"

namespace Game::Sim {

/// Player-input-driven weapon: each fixed tick the owner calls Tick(firing, ...);
/// a fire-rate cooldown gates shots. The active gun brain is the sole RNG stream.
class WeaponController {
public:
    enum class Kind { Single, HeatMinigun };

    struct Params {
        Kind kind = Kind::Single;
        float fireIntervalSeconds = 0.15F;
        float bulletSpeedPxPerSec = 120.0F;
        float lifeMs = 1500.0F;
        int damage = 1;
        // Bullet attributes carried into the FireIntent (faithful: RGBullet.UpdateAttribute):
        int critical = 0;        ///< crit chance 0..100 (WeaponDef.critical).
        float repel = 0.0F;      ///< knockback magnitude (WeaponDef.repel).
        bool canThrough = false; ///< pierce-through enabled (WeaponDef.canThrough).
        int pierce = 0;          ///< pass-through budget (WeaponDef.throughCount).
        // Multi-shot (WeaponDef.count/angle): count > 1 fires one deterministic Fan of
        // `count` bullets across `fanSpreadDeg` total, expanded by FireSystem. count <= 1
        // keeps the single-shot path. Independent of kind (HeatMinigun takes precedence).
        int count = 1;
        float fanSpreadDeg = 0.0F;
        // (d) brain path: raw per-pellet fan step (WeaponDef.angle), needed by the
        // gun-brain Fan adapters (distinct from fanSpreadDeg, the legacy total span).
        float fanStepDeg = 0.0F;
        // Single (Gun001):
        float baseAngle = 5.0F;
        float recoil = 0.0F;
        // HeatMinigun (Gun016):
        float heatMaxTime = 2.0F;
        float heatBaseAngle = 20.0F;
        float heatRecoil = 0.0F;
    };

    WeaponController(const Params &params, int seed);

    /// (d) dispatch ctor (B1-P3): drive a per-gun IWeaponBrain. The fire-rate
    /// cooldown gate stays here (passed to the brain as FireContext.canFire);
    /// the brain owns the pattern + any heat/charge/burst state.
    WeaponController(std::unique_ptr<IWeaponBrain> brain, const Params &params, int seed);

    /// Advance one fixed tick. Appends this tick's FireIntents (scattered, baked
    /// dir) to @p out. @return the number of trigger PULLS resolved this tick (for
    /// energy accounting): 1 when a pull fires (a Fan pull is 1, not its pellet
    /// count), 0 when idle/charging.
    int Tick(bool firing, glm::vec2 origin, glm::vec2 aimDir,
             std::vector<FireIntent> &out);

    float HeatTime() const { return m_Brain ? m_Brain->HeatTime() : m_HeatTime; } ///< for tests.

private:
    Params m_Params;
    std::unique_ptr<IWeaponBrain> m_Brain; ///< (d) gun brain; null on the legacy path.
    Gun001 m_Gun001;
    Gun016 m_Gun016;
    int m_CooldownTicks = 0;
    float m_HeatTime = 0.0F;
};

} // namespace Game::Sim

#endif /* GAME_SIM_WEAPONCONTROLLER_HPP */
