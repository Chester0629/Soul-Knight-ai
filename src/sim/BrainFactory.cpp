#include "sim/BrainFactory.hpp"

#include <algorithm>
#include <memory>

#include "sim/BodyGeom.hpp"
#include "sim/BossBrainAdapters.hpp"
#include "sim/EnemyBrainAdapters.hpp"
#include "sim/SimConfig.hpp"
#include "sim/WeaponBrainAdapters.hpp"

namespace Game::Sim {

EnemyController::Params BrainFactory::EnemyParams(const Game::EnemyDef &def) {
    EnemyController::Params p;
    // ENEMY RE-KEY: EnemyDef now carries a real per-species speed (FAITHFUL @
    // enemies.csv). A def with no species data (e.g. a default-constructed test
    // EnemyDef{}, or a future gap in the table) has speed==0 -- keep the old
    // slice default (60 px/s, PORT CHOICE (T-56), not a data value) rather than freezing the
    // enemy in place. P3.0: EnemyDef.speed is the RoleAttribute speed in Unity units/s
    // (3..12), so it goes through the single data-speed factor (1 unit = 32 px) to give the
    // controller's px/s steering speed.
    p.speed = def.speed > 0.0F ? def.speed * kDataSpeedToPxPerSec : 60.0F;
    p.speedRate = def.speedRate;
    p.friction = def.friction;
    p.scoutRateSeconds = def.scoutRate;
    p.shootCdSeconds = def.shootCd;
    p.kinematic = def.kinematic != 0;
    return p;
}

EnemyController BrainFactory::MakeEnemy(const Game::EnemyDef &def, glm::vec2 spawn, int seed) {
    // (d) dispatch: pick the brain adapter by AI CLASS (def.enemyClass ==
    // "EnemyAIxx"), not by species id -- many species (def.id) share one class.
    return EnemyController(MakeEnemyBrain(def.enemyClass), EnemyParams(def), spawn,
                          seed); // prvalue -> C++17 guaranteed elision
}

std::unique_ptr<EnemyController> BrainFactory::MakeEnemyPtr(const Game::EnemyDef &def,
                                                            glm::vec2 spawn, int seed) {
    return std::make_unique<EnemyController>(MakeEnemyBrain(def.enemyClass), EnemyParams(def),
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
                                          const std::string &weaponId, int seed,
                                          const Game::GameData *data) {
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
    // P3.2: resolve this weapon's own bullet's buff-application trigger (if any)
    // ONCE here, from GameData -- see MakeWeapon's doc comment.
    if (data != nullptr) {
        if (const Game::BulletDef *bulletDef = data->BulletForWeapon(def)) {
            // Bullet prefab collider -> hit radius (0 when the prefab has none -> fallback).
            p.bulletRadiusPx = EquivalentRadiusPx(bulletDef->collider);
            if (!bulletDef->buffId.empty()) {
                if (const Game::BuffDef *buffDef = data->FindBuff(bulletDef->buffId)) {
                    p.hasBuff = true;
                    p.buffParams = Game::BuffParamsFrom(*buffDef);
                    p.buffProbability = bulletDef->buffProbability;
                }
            }
        }
    }
    // (d) dispatch: pick the per-gun brain adapter by id (Gun001 fallback). The
    // brain returns the gun's REAL FirePattern; the cooldown/canFire gate stays in
    // WeaponController. Only Gun001+Gun016 were wired before; now all 21 dispatch.
    // WEAPON RE-KEY: weaponId may now be a PREFAB id (weapon_043); the gun brain
    // is selected by the def's class (Gun001..), falling back to the raw id.
    const std::string &brainId = def.weaponClass.empty() ? weaponId : def.weaponClass;
    return WeaponController(MakeWeaponBrain(brainId, def), p, seed); // prvalue
}

} // namespace Game::Sim
