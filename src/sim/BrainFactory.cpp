#include "sim/BrainFactory.hpp"

#include <algorithm>
#include <memory>

#include "sim/EnemyBrainAdapters.hpp"
#include "sim/SimConfig.hpp"

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
    // deviation feeds ONLY the Single (Gun001) cone. The Gun016/HeatMinigun path reads
    // heatBaseAngle, whose faithful source is a per-weapon-class recoil base (owner+0x30),
    // NOT this JSON field -- so it stays a slice default; do not route deviation into it.
    p.baseAngle = static_cast<float>(def.deviation);
    p.critical = def.critical;
    p.repel = def.repel;
    p.canThrough = def.canThrough != 0;
    p.pierce = def.throughCount;
    // Multi-shot: WeaponDef.count bullets fanned over (count-1) * angle-step total degrees.
    // FireSystem expands the Fan; count <= 1 leaves the single-shot path untouched. Gun016
    // stays a heat single-stream (kind takes precedence over count in WeaponController::Tick).
    p.count = def.count > 1 ? def.count : 1;
    p.fanSpreadDeg = def.angle * static_cast<float>(p.count - 1);
    return WeaponController(p, seed); // prvalue
}

} // namespace Game::Sim
