#ifndef GAME_FLOOR_CLEAR_HPP
#define GAME_FLOOR_CLEAR_HPP

#include <vector>

#include "sim/Simulation.hpp"

namespace Game {

/**
 * @brief Pure floor-clear predicate over the sim's AUTHORITATIVE entity views.
 *
 * True iff no enemy view is alive AND (there is no boss, or the boss view is dead).
 *
 * Reads the sim's authoritative liveness (`EntityView::alive`), NOT the scene's
 * render mirrors (`GameScene::m_Enemies` are nulled a frame LATER, when the view
 * sync runs -- using them would lag the decision by a frame). Engine-free and
 * unit-testable with fabricated view lists.
 *
 * Boss handling matches the current port: a boss spawns on EVERY floor (the farthest
 * room, `GameScene` OnEnter), so `hasBoss` is normally true and the boss must be dead
 * to clear. When `hasBoss` is false the boss view is ignored (boss-cycle selection is
 * a Phase-2 concern; this predicate is correct either way).
 */
inline bool AllHostilesDead(const std::vector<Sim::Simulation::EntityView> &enemyViews,
                            bool hasBoss,
                            const Sim::Simulation::EntityView &bossView) {
    for (const Sim::Simulation::EntityView &v : enemyViews) {
        if (v.alive) {
            return false;
        }
    }
    if (hasBoss && bossView.alive) {
        return false;
    }
    return true;
}

/**
 * @brief Convenience over a live @ref Sim::Simulation: the whole-floor clear test.
 *
 * Reads `EnemyViews()` / `HasBoss()` / `BossView()` (the authoritative sources).
 * `BossView()` is only evaluated when `HasBoss()` is true (it dereferences the boss
 * controller), mirroring `GameScene::RoomHasLiveHostile`'s guard.
 */
inline bool FloorCleared(const Sim::Simulation &sim) {
    return AllHostilesDead(sim.EnemyViews(), sim.HasBoss(),
                           sim.HasBoss() ? sim.BossView() : Sim::Simulation::EntityView{});
}

} // namespace Game

#endif /* GAME_FLOOR_CLEAR_HPP */
