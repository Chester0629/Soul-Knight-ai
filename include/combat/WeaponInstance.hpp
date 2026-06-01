#ifndef GAME_WEAPON_INSTANCE_HPP
#define GAME_WEAPON_INSTANCE_HPP

#include "data/GameData.hpp"

namespace Game {
/**
 * @class WeaponInstance
 * @brief Runtime fire-control for a player weapon.
 *
 * Wraps a WeaponDef and tracks the shot cooldown derived from the weapon's
 * fire-rate (@c weapon_speed). It answers "may I fire now?" and exposes the
 * per-shot energy cost. Energy gating itself is the caller's responsibility
 * (check/spend on the player's CombatStats), keeping this class focused on
 * timing.
 */
class WeaponInstance {
public:
    /// Base shot interval (ms) at weapon_speed == 1; scaled by 1 / weapon_speed.
    static constexpr float kBaseIntervalMs = 200.0F;

    explicit WeaponInstance(const WeaponDef &def);

    /// Tick the cooldown toward ready by @p dtMs milliseconds.
    void Update(float dtMs);

    /// Whether the weapon's cooldown has elapsed.
    bool Ready() const;

    /// If ready, start the cooldown and report a fired shot; else returns false.
    bool TryFire();

    /// Energy consumed per shot (from the weapon's @c consume).
    int EnergyCost() const;

    float CooldownRemainingMs() const { return m_CooldownMs; }
    float FireIntervalMs() const { return m_FireIntervalMs; }
    const WeaponDef &Def() const { return *m_Def; }

private:
    const WeaponDef *m_Def;
    float m_FireIntervalMs;
    float m_CooldownMs = 0.0F;
};
} // namespace Game

#endif /* GAME_WEAPON_INSTANCE_HPP */
