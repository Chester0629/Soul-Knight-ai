#include <gtest/gtest.h>

#include <cmath>

#include "combat/PlayerDash.hpp"

using Game::PlayerDash;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
// characters.json player_template: skill_cd = 2.5, in_skill_time = 5.0.
constexpr float kSkillCd = 2.5F;
constexpr float kInSkillTime = 5.0F;
constexpr glm::vec2 kRight{1.0F, 0.0F};
} // namespace

// --- cooldown gating ---------------------------------------------------------

TEST(PlayerDashTest, StartsReady) {
    PlayerDash d(kSkillCd, kInSkillTime);
    EXPECT_TRUE(d.SkillReady());
    EXPECT_FALSE(d.IsDashing());
    EXPECT_FLOAT_EQ(d.CooldownRemaining(), 0.0F);
}

TEST(PlayerDashTest, FirstDashSucceedsAndEntersActive) {
    PlayerDash d(kSkillCd, kInSkillTime);
    EXPECT_TRUE(d.TryDash(kRight, 20.0F));
    EXPECT_TRUE(d.IsDashing());
    // FAITHFUL: the cooldown is only spent at RoleSkillEnd (ReSetSkillReload), so
    // mid-dash this_skill_time is still at skill_cd and skill_ready stays true.
    // Re-firing is blocked by the in_skill gate, NOT by the cooldown.
    EXPECT_TRUE(d.SkillReady());
    EXPECT_FALSE(d.TryDash(kRight, 20.0F)); // in_skill gate prevents re-fire
}

TEST(PlayerDashTest, CannotDashWhileAlreadyDashing) {
    PlayerDash d(kSkillCd, kInSkillTime);
    EXPECT_TRUE(d.TryDash(kRight, 20.0F));
    EXPECT_FALSE(d.TryDash(kRight, 20.0F)); // in_skill gate
}

TEST(PlayerDashTest, CooldownGatesReDash) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, 20.0F));

    // Finish the active window (5.0s); cooldown resets to 0 at end.
    d.Tick(kInSkillTime * 1000.0F);
    EXPECT_FALSE(d.IsDashing());
    EXPECT_FALSE(d.SkillReady());
    EXPECT_FLOAT_EQ(d.CooldownRemaining(), kSkillCd);

    // Still cooling down -> cannot dash.
    d.Tick(1000.0F); // +1.0s of 2.5s
    EXPECT_FALSE(d.SkillReady());
    EXPECT_FALSE(d.TryDash(kRight, 20.0F));
    EXPECT_NEAR(d.CooldownRemaining(), 1.5F, 1e-4F);

    // Reach skill_cd -> ready, can dash again.
    d.Tick(1500.0F); // +1.5s -> total 2.5s
    EXPECT_TRUE(d.SkillReady());
    EXPECT_FLOAT_EQ(d.CooldownRemaining(), 0.0F);
    EXPECT_TRUE(d.TryDash(kRight, 20.0F));
}

TEST(PlayerDashTest, CooldownClampsAtSkillCd) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, 20.0F));
    d.Tick(kInSkillTime * 1000.0F); // end dash, cooldown -> 0
    d.Tick(100000.0F);              // over-recharge
    EXPECT_TRUE(d.SkillReady());
    EXPECT_FLOAT_EQ(d.CooldownRemaining(), 0.0F);
    // skill_ready is skill_cd <= this_skill_time, clamped so it never overshoots.
    EXPECT_TRUE(d.TryDash(kRight, 20.0F));
}

TEST(PlayerDashTest, CooldownResetsAtEndOfDashNotStart) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, 20.0F));
    d.Tick(1000.0F); // 1s into the 5s active window
    EXPECT_TRUE(d.IsDashing());
    // FAITHFUL: ReSetSkillReload runs in RoleSkillEnd, so mid-dash the cooldown is
    // untouched (still at skill_cd from the ready state, CooldownRemaining == 0).
    EXPECT_FLOAT_EQ(d.CooldownRemaining(), 0.0F);
    EXPECT_TRUE(d.SkillReady());

    // Finish the window -> RoleSkillEnd resets this_skill_time to 0 -> full cooldown.
    d.Tick((kInSkillTime - 1.0F) * 1000.0F);
    EXPECT_FALSE(d.IsDashing());
    EXPECT_FLOAT_EQ(d.CooldownRemaining(), kSkillCd);
    EXPECT_FALSE(d.SkillReady());
}

// --- dash duration -----------------------------------------------------------

TEST(PlayerDashTest, DashLastsInSkillTime) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, 20.0F));

    d.Tick(4000.0F); // 4s < 5s
    EXPECT_TRUE(d.IsDashing());
    EXPECT_NEAR(d.DashTimeRemaining(), 1.0F, 1e-4F);

    d.Tick(1000.0F); // total 5s -> ends
    EXPECT_FALSE(d.IsDashing());
    EXPECT_FLOAT_EQ(d.DashTimeRemaining(), 0.0F);
}

TEST(PlayerDashTest, DashEndAtExactBoundary) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, 20.0F));
    d.Tick(kInSkillTime * 1000.0F); // exactly in_skill_time -> >= ends it
    EXPECT_FALSE(d.IsDashing());
}

// --- force cap ---------------------------------------------------------------

TEST(PlayerDashTest, ForceCapsAtThirty) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, 100.0F)); // request 100 -> clamp to 30
    EXPECT_FLOAT_EQ(d.InertialVel(), PlayerDash::kForceCap);
    EXPECT_FLOAT_EQ(d.InertialVel(), 30.0F);
}

TEST(PlayerDashTest, ForceBelowCapPassesThrough) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, 12.5F));
    EXPECT_FLOAT_EQ(d.InertialVel(), 12.5F);
}

TEST(PlayerDashTest, NegativeForceFloorsAtZero) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, -5.0F));
    EXPECT_FLOAT_EQ(d.InertialVel(), 0.0F);
}

// --- impulse direction + per-step velocity -----------------------------------

TEST(PlayerDashTest, DirectionIsNormalized) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(glm::vec2(3.0F, 4.0F), 20.0F)); // |(3,4)| = 5
    const glm::vec2 dir = d.ForceDirection();
    EXPECT_NEAR(dir.x, 0.6F, 1e-4F);
    EXPECT_NEAR(dir.y, 0.8F, 1e-4F);
    EXPECT_NEAR(std::sqrt(dir.x * dir.x + dir.y * dir.y), 1.0F, 1e-4F);
}

TEST(PlayerDashTest, ZeroDirectionYieldsZeroImpulseButStillDashes) {
    PlayerDash d(kSkillCd, kInSkillTime);
    // GetForce(move_dir=0): impulse direction zero, still consumes the skill.
    EXPECT_TRUE(d.TryDash(glm::vec2(0.0F, 0.0F), 20.0F));
    EXPECT_TRUE(d.IsDashing());
    const glm::vec2 v = d.CurrentDashVelocity();
    EXPECT_FLOAT_EQ(v.x, 0.0F);
    EXPECT_FLOAT_EQ(v.y, 0.0F);
}

TEST(PlayerDashTest, CurrentDashVelocityMatchesForceTimesInertia) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, 20.0F));
    const glm::vec2 v = d.CurrentDashVelocity();
    EXPECT_FLOAT_EQ(v.x, 20.0F); // (1,0) * 20
    EXPECT_FLOAT_EQ(v.y, 0.0F);
}

TEST(PlayerDashTest, NoDashVelocityWhenNotDashing) {
    PlayerDash d(kSkillCd, kInSkillTime);
    const glm::vec2 v = d.CurrentDashVelocity();
    EXPECT_FLOAT_EQ(v.x, 0.0F);
    EXPECT_FLOAT_EQ(v.y, 0.0F);
}

TEST(PlayerDashTest, ImpulseDecayBelowThresholdStopsContributing) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, 2.0F)); // just above the >1 threshold
    EXPECT_GT(d.CurrentDashVelocity().x, 0.0F);
    d.DecayImpulse(0.4F); // 2.0 * 0.4 = 0.8 <= 1 threshold
    EXPECT_NEAR(d.InertialVel(), 0.8F, 1e-4F);
    EXPECT_FLOAT_EQ(d.CurrentDashVelocity().x, 0.0F); // no longer plays
}

TEST(PlayerDashTest, DecayClampsFrictionFactorToOne) {
    PlayerDash d(kSkillCd, kInSkillTime);
    ASSERT_TRUE(d.TryDash(kRight, 20.0F));
    d.DecayImpulse(2.0F); // Mathf.Min(1f, friction) -> 1, no growth
    EXPECT_FLOAT_EQ(d.InertialVel(), 20.0F);
}

// --- determinism -------------------------------------------------------------

TEST(PlayerDashTest, DeterministicAcrossInstances) {
    PlayerDash a(kSkillCd, kInSkillTime);
    PlayerDash b(kSkillCd, kInSkillTime);
    for (int i = 0; i < 50; ++i) {
        const bool ra = a.TryDash(kRight, 20.0F);
        const bool rb = b.TryDash(kRight, 20.0F);
        EXPECT_EQ(ra, rb);
        a.Tick(250.0F);
        b.Tick(250.0F);
        EXPECT_EQ(a.IsDashing(), b.IsDashing());
        EXPECT_FLOAT_EQ(a.CooldownRemaining(), b.CooldownRemaining());
        EXPECT_FLOAT_EQ(a.InertialVel(), b.InertialVel());
    }
}

// NOLINTEND(readability-magic-numbers)
