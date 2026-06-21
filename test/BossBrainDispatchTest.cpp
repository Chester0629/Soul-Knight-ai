#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "sim/BossController.hpp"
#include "sim/BrainFactory.hpp"
#include "sim/FireIntent.hpp"
#include "sim/Scheduler.hpp"

using Game::Sim::BossController;
using Game::Sim::BrainFactory;

// NOLINTBEGIN(readability-magic-numbers)

// === B1-P2 3.1: (d) function-adapter dispatch for bosses =====================
// BrainFactory routes a boss id -> the matching brain adapter, so the SAME
// BossController drives HETEROGENEOUS bosses (no controller branching, brain
// source unchanged). The BossAI01 path stays byte-identical (BossControllerTest).

namespace {

struct DriveOut {
    std::vector<float> moveTrace; ///< moveDir.x, moveDir.y per tick.
    int fires = 0;                ///< projectiles emitted.
};

// Activate the boss, tick it, and capture its per-tick move decision + shots.
DriveOut Drive(BossController &b, int ticks) {
    Game::Sim::Scheduler sched;
    std::vector<Game::Sim::FireIntent> fire;
    b.MutableState().awake = true;
    b.SetTarget(glm::vec2{50.0F, 0.0F}); // chase axis-aligned to +x.
    b.Activate(sched, fire);
    DriveOut out;
    for (int i = 0; i < ticks; ++i) {
        sched.Tick();
        out.moveTrace.push_back(b.MoveDir().x);
        out.moveTrace.push_back(b.MoveDir().y);
    }
    out.fires = static_cast<int>(fire.size());
    return out;
}

} // namespace

// AI01 (ChaseMoveDecision: target-relative) and AI08 (WanderDirection: random
// heading) compute DIFFERENT move vectors from the SAME seeded stream, so with an
// identical seed their move traces diverge -> the factory dispatched two distinct
// brains, not one. (Their draw COUNTS match, so an RNG-position probe can't tell
// them apart; the behaviour does.)
TEST(BossBrainDispatchTest, AI01AndAI08DispatchToDistinctBrains) {
    auto ai01 = BrainFactory::MakeBossPtr("BossAI01", 0.04F, glm::vec2{0, 0}, 600, 7);
    auto ai08 = BrainFactory::MakeBossPtr("BossAI08", 0.04F, glm::vec2{0, 0}, 600, 7);
    const DriveOut a = Drive(*ai01, 40);
    const DriveOut b = Drive(*ai08, 40);
    EXPECT_NE(a.moveTrace, b.moveTrace) << "AI01 chase != AI08 wander -> distinct brains";
}

// AI01's with-target move decision is axis-aligned to the chase dir: for a +x
// chase every branch (chase / retreat / strafe-x / strafe-y) keeps y == 0. AI08's
// wander draws a free heading, so its y is generically non-zero. A non-zero AI08
// moveDir.y therefore proves the AI08 adapter (not AI01) was dispatched.
TEST(BossBrainDispatchTest, AI08WandersOffTheChaseAxisButAI01DoesNot) {
    auto ai01 = BrainFactory::MakeBossPtr("BossAI01", 0.04F, glm::vec2{0, 0}, 600, 13);
    auto ai08 = BrainFactory::MakeBossPtr("BossAI08", 0.04F, glm::vec2{0, 0}, 600, 13);
    const DriveOut a = Drive(*ai01, 40);
    const DriveOut b = Drive(*ai08, 40);
    float ai01MaxAbsY = 0.0F;
    float ai08MaxAbsY = 0.0F;
    for (std::size_t i = 1; i < a.moveTrace.size(); i += 2) {
        ai01MaxAbsY = (std::max)(ai01MaxAbsY, std::abs(a.moveTrace[i]));
    }
    for (std::size_t i = 1; i < b.moveTrace.size(); i += 2) {
        ai08MaxAbsY = (std::max)(ai08MaxAbsY, std::abs(b.moveTrace[i]));
    }
    EXPECT_FLOAT_EQ(ai01MaxAbsY, 0.0F) << "AI01 chase stays on the +x axis";
    EXPECT_GT(ai08MaxAbsY, 0.0F) << "AI08 wanders off-axis -> AI08 brain dispatched";
}

// A shooter boss (AI11, field-gated ShootReflection) actually emits projectiles
// through its adapter -> the live game shows a boss that shoots (Fan-approx).
TEST(BossBrainDispatchTest, AI11ShooterFiresProjectiles) {
    auto b = BrainFactory::MakeBossPtr("BossAI11", 0.04F, glm::vec2{0, 0}, 600, 5);
    const DriveOut out = Drive(*b, 60);
    EXPECT_GT(out.fires, 0) << "AI11 shooter must emit projectiles via its adapter";
}

// All 14 standard fighting bosses dispatch + tick through their own adapter
// without crashing. (BossAI06Child/BossAI12Parent are deferred sub-entities, not
// registered fighting brains.)
TEST(BossBrainDispatchTest, AllFourteenStandardBossesDispatchAndTick) {
    const char *ids[] = {"BossAI01", "BossAI02", "BossAI03", "BossAI04", "BossAI05",
                         "BossAI06", "BossAI07", "BossAI08", "BossAI09", "BossAI10",
                         "BossAI11", "BossAI12", "BossAI13", "BossAI14"};
    for (const char *id : ids) {
        auto b = BrainFactory::MakeBossPtr(id, 0.04F, glm::vec2{0, 0}, 600, 3);
        ASSERT_NE(b, nullptr) << id;
        Drive(*b, 30); // ticks through its own adapter (move + attack) -- no crash.
    }
}

// Every boss in the GameScene C spawn roster actually fires (Fan-approx) through
// its adapter -> the live boss room shows a boss that shoots.
TEST(BossBrainDispatchTest, RosterCBossesAllFire) {
    for (const char *id : {"BossAI01", "BossAI08", "BossAI11", "BossAI06", "BossAI12"}) {
        auto b = BrainFactory::MakeBossPtr(id, 0.04F, glm::vec2{0, 0}, 600, 9);
        EXPECT_GT(Drive(*b, 80).fires, 0) << id << " must fire via its adapter";
    }
}

// BossAI03's attack is self-gated on can_shoot with no public opener (anim state
// machine), so this pass is MOVE-ONLY: it dispatches + moves but fires nothing.
// Documents the recorded gated-boss debt (AI03 is excluded from the spawn roster).
TEST(BossBrainDispatchTest, GatedAI03DispatchesMoveOnly) {
    auto b = BrainFactory::MakeBossPtr("BossAI03", 0.04F, glm::vec2{0, 0}, 600, 4);
    EXPECT_EQ(Drive(*b, 80).fires, 0) << "AI03 gated -> no fire (debt, not a bug)";
}

// Unknown id falls back to the BossAI01 adapter: same seed -> the AI01 brain's
// move trace, identical to an explicit "BossAI01".
TEST(BossBrainDispatchTest, UnknownIdFallsBackToAI01) {
    auto unknown = BrainFactory::MakeBossPtr("BossAI_nope", 0.04F, glm::vec2{0, 0}, 600, 21);
    auto ai01 = BrainFactory::MakeBossPtr("BossAI01", 0.04F, glm::vec2{0, 0}, 600, 21);
    EXPECT_EQ(Drive(*unknown, 30).moveTrace, Drive(*ai01, 30).moveTrace);
}

// A dispatched non-AI01 boss replays byte-identically from the same seed.
TEST(BossBrainDispatchTest, DispatchedBossReplayIsDeterministic) {
    auto run = [] {
        auto b = BrainFactory::MakeBossPtr("BossAI08", 0.04F, glm::vec2{0, 0}, 600, 99);
        const DriveOut o = Drive(*b, 50);
        return o.moveTrace;
    };
    EXPECT_EQ(run(), run());
}

// NOLINTEND(readability-magic-numbers)
