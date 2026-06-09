#include "sim/BrainFactory.hpp"

#include <algorithm>

#include "sim/SimConfig.hpp"

namespace Game::Sim {

EnemyController BrainFactory::MakeEnemy(const Game::EnemyDef &def, glm::vec2 spawn,
                                        int seed) {
    EnemyController::Params p;
    p.speed = 60.0F;                      // slice default (EnemyDef has no per-enemy speed)
    p.speedRate = 0.0F;                   // slice default (EnemyDef has no speed_rate field)
    p.friction = def.friction;
    p.scoutRateSeconds = def.scoutRate;
    p.shootCdSeconds = def.shootCd;
    p.kinematic = def.kinematic != 0;
    return EnemyController(p, spawn, seed); // prvalue -> C++17 guaranteed copy elision
}

BossController BrainFactory::MakeBoss(float baseShootCd, glm::vec2 spawn, int maxHp,
                                      int seed) {
    // prvalue: BossController is move-deleted, so C++17 guaranteed elision constructs
    // it directly into the caller's storage (no move/copy ctor required).
    return BossController(baseShootCd, spawn, maxHp, seed);
}

WeaponController BrainFactory::MakeWeapon(const Game::WeaponDef &def,
                                          const std::string &weaponId, int seed) {
    WeaponController::Params p; // p.fireIntervalSeconds defaults to the slice base (0.15s).
    p.kind = (weaponId == "Gun016") ? WeaponController::Kind::HeatMinigun
                                     : WeaponController::Kind::Single;
    // weaponSpeed is a fire-rate MULTIPLIER (RGWeapon); the true base cadence is
    // OWNER/animation-driven (Plan 4). Until wired, scale the slice base by it so a
    // faster weaponSpeed shortens the interval. Guard a zero/negative multiplier.
    p.fireIntervalSeconds /= (std::max)(0.01F, def.weaponSpeed);
    p.bulletSpeedPxPerSec = def.bulletSpeed * kDataSpeedToPxPerSec;
    p.damage = def.atk;
    p.baseAngle = static_cast<float>(def.deviation);
    return WeaponController(p, seed); // prvalue
}

} // namespace Game::Sim
