#include "sim/BrainFactory.hpp"

#include <algorithm>
#include <memory>

#include "sim/BossBrainAdapters.hpp"
#include "sim/EnemyBrainAdapters.hpp"
#include "sim/SimConfig.hpp"
#include "sim/WeaponBrainAdapters.hpp"

namespace Game::Sim {

EnemyController::Params BrainFactory::EnemyParams(const Game::EnemyDef &def) {
    EnemyController::Params p;
    p.speed = 60.0F;     // slice default (EnemyDef has no per-enemy speed)
    p.speedRate = 0.0F;  // slice default (EnemyDef has no speed_rate field)
    p.friction = def.friction;
    p.scoutRateSeconds = def.scoutRate;
    p.shootCdSeconds = def.shootCd;
    p.kinematic = def.kinematic != 0;
    return p;
}

EnemyController BrainFactory::MakeEnemy(const Game::EnemyDef &def, glm::vec2 spawn, int seed) {
    // (d) dispatch: pick the brain adapter by enemy id (def.id == "EnemyAIxx").
    return EnemyController(MakeEnemyBrain(def.id), EnemyParams(def), spawn,
                          seed); // prvalue -> C++17 guaranteed elision
}

std::unique_ptr<EnemyController> BrainFactory::MakeEnemyPtr(const Game::EnemyDef &def,
                                                            glm::vec2 spawn, int seed) {
    return std::make_unique<EnemyController>(MakeEnemyBrain(def.id), EnemyParams(def),
                                             spawn, seed);
}

BossController BrainFactory::MakeBoss(float baseShootCd, glm::vec2 spawn, int maxHp,
                                      int seed) {
    // prvalue: BossController is move-deleted, so C++17 guaranteed elision constructs
    // it directly into the caller's storage (no move/copy ctor required).
    return BossController(baseShootCd, spawn, maxHp, seed);
}

std::unique_ptr<BossController> BrainFactory::MakeBossPtr(const std::string &bossId,
                                                         float baseShootCd, glm::vec2 spawn,
                                                         int maxHp, int seed) {
    // (d) dispatch: pick the boss brain adapter by id, inject into the controller.
    return std::make_unique<BossController>(MakeBossBrain(bossId, baseShootCd), baseShootCd,
                                            spawn, maxHp, seed);
}

WeaponController BrainFactory::MakeWeapon(const Game::WeaponDef &def,
                                          const std::string &weaponId, int seed) {
    WeaponController::Params p; // p.fireIntervalSeconds defaults to the slice base (0.15s).
    // weaponSpeed is a fire-rate MULTIPLIER (RGWeapon); the true base cadence is
    // OWNER/animation-driven (Plan 4). Until wired, scale the slice base by it so a
    // faster weaponSpeed shortens the interval. Guard a zero/negative multiplier.
    p.fireIntervalSeconds /= (std::max)(0.01F, def.weaponSpeed);
    p.bulletSpeedPxPerSec = def.bulletSpeed * kDataSpeedToPxPerSec;
    p.damage = def.atk;
    // deviation is the scatter base angle the gun-brain adapters widen by recoil.
    p.baseAngle = static_cast<float>(def.deviation);
    p.recoil = 0.0F; // owner recoil multiplier (owner+0x20) unrecovered; 0 -> cone == deviation (debt).
    p.critical = def.critical;
    p.repel = def.repel;
    p.canThrough = def.canThrough != 0;
    p.pierce = def.throughCount;
    // Fan data: raw count + per-pellet step (def.angle) for the (d) Fan adapters;
    // fanSpreadDeg kept for the legacy data-Fan regression path.
    p.count = def.count;
    p.fanStepDeg = def.angle;
    p.fanSpreadDeg = def.angle * static_cast<float>((def.count > 1 ? def.count : 1) - 1);
    // (d) dispatch: pick the per-gun brain adapter by id (Gun001 fallback). The
    // brain returns the gun's REAL FirePattern; the cooldown/canFire gate stays in
    // WeaponController. Only Gun001+Gun016 were wired before; now all 21 dispatch.
    return WeaponController(MakeWeaponBrain(weaponId, def), p, seed); // prvalue
}

} // namespace Game::Sim
