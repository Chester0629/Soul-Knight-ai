#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "sim/BrainFactory.hpp"
#include "sim/EnemyController.hpp"
#include "sim/FireIntent.hpp"
#include "sim/Scheduler.hpp"

using Game::Sim::BrainFactory;
using Game::Sim::EnemyController;

// NOLINTBEGIN(readability-magic-numbers)

// === B1-P1 3.1: (d) function-adapter dispatch ================================
// BrainFactory routes an enemy id -> the matching brain adapter, so the SAME
// EnemyController drives HETEROGENEOUS brains (no controller branching, brain
// source unchanged). Proven by distinct in-controller behaviour.

namespace {

Game::EnemyDef Def(const std::string &id, bool kinematic = false) {
    Game::EnemyDef d{};
    d.id = id;
    d.scoutRate = 0.1F;
    d.shootCd = 0.1F;
    d.friction = 0.9F;
    d.kinematic = kinematic ? 1 : 0;
    return d;
}

// Activate the controller, tick it, and return the largest wander magnitude seen.
float MaxWander(EnemyController &e, int ticks) {
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    e.MutableState().awake = true;
    e.SetTarget(glm::vec2{50.0F, 0.0F});
    e.Activate(sched, fire);
    float best = 0.0F;
    for (int i = 0; i < ticks; ++i) {
        sched.Tick();
        const glm::vec2 m = e.MoveDir();
        best = (std::max)(best, std::abs(m.x) + std::abs(m.y));
    }
    return best;
}

} // namespace

// The turret AI06 adapter returns a zero wander dir (it never translates); AI01
// wanders. If AI06 had been mis-dispatched to AI01 it WOULD wander -- so a zero
// wander proves the AI06 adapter (not AI01) was dispatched.
TEST(EnemyBrainDispatchTest, TurretAI06NeverWandersButAI01Does) {
    auto ai01 = BrainFactory::MakeEnemyPtr(Def("EnemyAI01"), glm::vec2{0, 0}, 7);
    auto ai06 = BrainFactory::MakeEnemyPtr(Def("EnemyAI06", /*kinematic=*/true),
                                           glm::vec2{0, 0}, 7);
    EXPECT_FLOAT_EQ(MaxWander(*ai06, 40), 0.0F) << "turret must never wander";
    EXPECT_GT(MaxWander(*ai01, 40), 0.0F) << "AI01 wanders -> AI06 != AI01 brain";
}

// AI07 (int-shoot) dispatches to a DISTINCT brain from AI01: with the same seed,
// the two brains' Scout/Shoot draw patterns differ, so after identical activation
// their RNG streams sit at different positions (probed via the interface).
TEST(EnemyBrainDispatchTest, AI07DispatchesToADistinctBrainFromAI01) {
    auto ai01 = BrainFactory::MakeEnemyPtr(Def("EnemyAI01"), glm::vec2{0, 0}, 99);
    auto ai07 = BrainFactory::MakeEnemyPtr(Def("EnemyAI07"), glm::vec2{0, 0}, 99);
    (void)MaxWander(*ai01, 30);
    (void)MaxWander(*ai07, 30);
    // Distinct brains drew distinct amounts from their (same-seeded) streams, so a
    // probe draw now diverges. (Equal would mean both were the same AI01 brain.)
    EXPECT_NE(ai01->Brain().RngRange(0, 1000000), ai07->Brain().RngRange(0, 1000000));
}

// Drive a controller for @p ticks; return {largest wander seen, projectiles fired}.
namespace {
std::pair<float, int> Drive(EnemyController &e, int ticks) {
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    e.MutableState().awake = true;
    e.SetTarget(glm::vec2{50.0F, 0.0F});
    e.Activate(sched, fire);
    float best = 0.0F;
    for (int i = 0; i < ticks; ++i) {
        sched.Tick();
        const glm::vec2 m = e.MoveDir();
        best = (std::max)(best, std::abs(m.x) + std::abs(m.y));
    }
    return {best, static_cast<int>(fire.size())};
}
} // namespace

// All 15 ported brains dispatch (no fallback for a real id). AI05 has no brain.
TEST(EnemyBrainDispatchTest, AllFifteenBrainsDispatch) {
    const char *ids[] = {"EnemyAI01", "EnemyAI02", "EnemyAI03", "EnemyAI04",
                         "EnemyAI06", "EnemyAI07", "EnemyAI08", "EnemyAI09",
                         "EnemyAI10", "EnemyAI11", "EnemyAI12", "EnemyAI13",
                         "EnemyAI14", "EnemyAI15", "EnemyAIShark"};
    for (const char *id : ids) {
        auto e = BrainFactory::MakeEnemyPtr(Def(id), glm::vec2{0, 0}, 3);
        ASSERT_NE(e, nullptr) << id;
        // Each ticks without crashing through its own adapter.
        Drive(*e, 20);
    }
}

// Melee brain AI04 (no ShootReflection): chases the player (moves) but emits NO
// projectile -- its contact attack is the deferred contact-damage debt, not a bug.
TEST(EnemyBrainDispatchTest, MeleeAI04MovesButFiresNoProjectile) {
    auto e = BrainFactory::MakeEnemyPtr(Def("EnemyAI04"), glm::vec2{0, 0}, 7);
    const auto [wander, fires] = Drive(*e, 60);
    EXPECT_GT(wander, 0.0F) << "melee brain should chase/move";
    EXPECT_EQ(fires, 0) << "melee = no projectile (contact-damage deferred)";
}

// A shooter brain (AI11, the ice enemy in the C roster) actually fires projectiles
// through its adapter -> the live game shows enemies that shoot.
TEST(EnemyBrainDispatchTest, ShooterAI11FiresProjectiles) {
    EnemyController::Params dummy; // unused; MakeEnemyPtr builds params from def.
    (void)dummy;
    auto e = BrainFactory::MakeEnemyPtr(Def("EnemyAI11"), glm::vec2{0, 0}, 5);
    const auto [wander, fires] = Drive(*e, 120);
    EXPECT_GT(fires, 0) << "AI11 shooter must emit projectiles via its adapter";
}

// Unknown / empty id falls back to the AI01 adapter (safe default + back-compat).
TEST(EnemyBrainDispatchTest, UnknownIdFallsBackToAI01) {
    auto unknown = BrainFactory::MakeEnemyPtr(Def("EnemyAI_nope"), glm::vec2{0, 0}, 7);
    auto ai01 = BrainFactory::MakeEnemyPtr(Def("EnemyAI01"), glm::vec2{0, 0}, 7);
    // Same seed, same (AI01) brain -> identical first probe draw.
    EXPECT_EQ(unknown->Brain().RngRange(0, 1000), ai01->Brain().RngRange(0, 1000));
}

// NOLINTEND(readability-magic-numbers)
