#ifndef GAME_SIM_BRAINFACTORY_HPP
#define GAME_SIM_BRAINFACTORY_HPP

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "sim/BossController.hpp"
#include "sim/EnemyController.hpp"
#include "sim/WeaponController.hpp"

namespace Game::Sim {

/// Builds sim controllers from data definitions. The single place that maps a
/// content def to its controller config; the extension point for the rest of the
/// roster in later cycles.
class BrainFactory {
public:
    /// Build an EnemyController from an EnemyDef at @p spawn, seeded with @p seed.
    static EnemyController MakeEnemy(const Game::EnemyDef &def, glm::vec2 spawn, int seed);

    /// The EnemyDef -> Params mapping (shared by MakeEnemy + MakeEnemyPtr).
    static EnemyController::Params EnemyParams(const Game::EnemyDef &def);

    /// Heap-construct an EnemyController in place. EnemyController is move-deleted, so a
    /// by-value factory prvalue cannot be moved into a unique_ptr; make_unique forwards the
    /// ctor args instead (no move).
    static std::unique_ptr<EnemyController> MakeEnemyPtr(const Game::EnemyDef &def,
                                                         glm::vec2 spawn, int seed);

    /// Build a BossController (BossAI01) at @p spawn. Returned by value (prvalue);
    /// the owner must store it stably (e.g. unique_ptr) -- the controller is move-deleted.
    static BossController MakeBoss(float baseShootCd, glm::vec2 spawn, int maxHp, int seed);

    /// (d) dispatch: heap-construct a BossController driving the @p bossId brain
    /// ("BossAI01".."BossAI14"; unknown -> BossAI01 fallback). BossController is
    /// move-deleted, so make_unique forwards the ctor args (no move).
    static std::unique_ptr<BossController> MakeBossPtr(const std::string &bossId,
                                                       float baseShootCd, glm::vec2 spawn,
                                                       int maxHp, int seed);

    /// Build a WeaponController from a WeaponDef. @p weaponId selects the gun brain
    /// ("Gun016" -> HeatMinigun, else Single). Returned by value (prvalue).
    static WeaponController MakeWeapon(const Game::WeaponDef &def,
                                       const std::string &weaponId, int seed);
};

} // namespace Game::Sim

#endif /* GAME_SIM_BRAINFACTORY_HPP */
