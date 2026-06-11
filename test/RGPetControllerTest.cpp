#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "combat/RGPetController.hpp"
#include "data/RGRandom.hpp"

using Game::RGPetController;
using MoveBranch = Game::RGPetController::MoveBranch;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Tight float compare for the deterministic scalar/vector formulas.
void ExpectVec2Near(const glm::vec2 &a, const glm::vec2 &b, float eps = 1e-5F) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
}

} // namespace

// ---- ctor-default constants -----------------------------------------------

TEST(RGPetControllerTest, CtorDefaultConstants) {
    // RGPetController___ctor @ game_full.c:427154-427166.
    EXPECT_FLOAT_EQ(RGPetController::kMinDistance, 100.0F); // 0x58
    EXPECT_FLOAT_EQ(RGPetController::kReplyTime1, 4.0F);    // 0x5C
    EXPECT_FLOAT_EQ(RGPetController::kReplyTime2, 2.0F);    // 0x60
    EXPECT_FLOAT_EQ(RGPetController::kScoutRate, 1.0F);     // 0x68
    EXPECT_FLOAT_EQ(RGPetController::kAtkCd, 2.0F);         // 0x6C
    EXPECT_EQ(RGPetController::kInitialFacing, 1);          // 0x78
    EXPECT_TRUE(RGPetController::kCanAtkInitial);           // 0x71
    EXPECT_EQ(RGPetController::kHealDivisor, 5);            // line 427344
    EXPECT_FLOAT_EQ(RGPetController::kFollowDecelThreshold, 1.0F); // line 427270
}

// ---- FixedUpdate: branch selection ----------------------------------------

TEST(RGPetControllerTest, FixedUpdateFollowBranchWhenDecelAtOrBelowThreshold) {
    // decel <= 1.0 -> FOLLOW (line 427270). Boundary: decel == 1.0 is still FOLLOW.
    RGPetController p;
    p.SetMoveDirection({2.0F, -3.0F});
    p.SetDecel(1.0F); // exactly at the threshold
    MoveBranch branch = MoveBranch::COAST;
    const glm::vec2 v = p.FixedUpdate(5.0F, 0.5F, branch);
    EXPECT_EQ(branch, MoveBranch::FOLLOW);
    // move_direction * speed * (speed_rate + 1) = (2,-3) * 5 * 1.5
    ExpectVec2Near(v, glm::vec2(2.0F, -3.0F) * 5.0F * 1.5F);
    EXPECT_FLOAT_EQ(p.Decel(), 1.0F); // FOLLOW must NOT decay decel
}

TEST(RGPetControllerTest, FixedUpdateFollowFormulaSpeedRatePlusOne) {
    // velocity = move_direction * speed * (speed_rate + 1.0)  (lines 427281-427288)
    RGPetController p;
    p.SetMoveDirection({1.0F, 0.0F});
    p.SetDecel(0.0F); // FOLLOW
    MoveBranch branch = MoveBranch::COAST;
    const glm::vec2 v = p.FixedUpdate(4.0F, 2.0F, branch);
    EXPECT_EQ(branch, MoveBranch::FOLLOW);
    // (1,0) * 4 * (2+1) = (12, 0)
    ExpectVec2Near(v, glm::vec2(12.0F, 0.0F));
}

TEST(RGPetControllerTest, FixedUpdateCoastBranchWhenDecelAboveThreshold) {
    // decel > 1.0 -> COAST: velocity = force_direction * decel, then decel *= damping.
    RGPetController p;
    p.SetForceDirection({0.0F, 1.0F});
    p.SetDecel(4.0F);
    p.SetDamping(0.5F);
    MoveBranch branch = MoveBranch::FOLLOW;
    const glm::vec2 v = p.FixedUpdate(99.0F, 99.0F, branch); // speed args unused in COAST
    EXPECT_EQ(branch, MoveBranch::COAST);
    // velocity = force_direction * decel = (0,1) * 4 = (0,4)  (line 427302)
    ExpectVec2Near(v, glm::vec2(0.0F, 4.0F));
    // decel *= damping => 4 * 0.5 = 2  (line 427309 write-back)
    EXPECT_FLOAT_EQ(p.Decel(), 2.0F);
}

TEST(RGPetControllerTest, FixedUpdateCoastDecelDecaysGeometrically) {
    // Repeated COAST ticks decay decel by damping each step until it drops to/below
    // the threshold and the branch flips back to FOLLOW.
    RGPetController p;
    p.SetForceDirection({1.0F, 0.0F});
    p.SetDecel(8.0F);
    p.SetDamping(0.5F);
    MoveBranch branch = MoveBranch::FOLLOW;

    p.FixedUpdate(0.0F, 0.0F, branch); // decel 8 -> COAST, write-back 4
    EXPECT_EQ(branch, MoveBranch::COAST);
    EXPECT_FLOAT_EQ(p.Decel(), 4.0F);

    p.FixedUpdate(0.0F, 0.0F, branch); // decel 4 -> COAST, write-back 2
    EXPECT_EQ(branch, MoveBranch::COAST);
    EXPECT_FLOAT_EQ(p.Decel(), 2.0F);

    p.FixedUpdate(0.0F, 0.0F, branch); // decel 2 -> COAST, write-back 1
    EXPECT_EQ(branch, MoveBranch::COAST);
    EXPECT_FLOAT_EQ(p.Decel(), 1.0F);

    // decel is now exactly 1.0 -> FOLLOW branch, no further decay.
    p.SetMoveDirection({3.0F, 0.0F});
    const glm::vec2 v = p.FixedUpdate(2.0F, 0.0F, branch);
    EXPECT_EQ(branch, MoveBranch::FOLLOW);
    ExpectVec2Near(v, glm::vec2(6.0F, 0.0F)); // (3,0)*2*(0+1)
    EXPECT_FLOAT_EQ(p.Decel(), 1.0F);
}

// ---- ReplyingHP: self-heal cadence ----------------------------------------

TEST(RGPetControllerTest, ReplyingHPNoHealWhenAtFullHp) {
    // Gate: only while hp < max_hp (line 427333). At full HP -> no timer advance.
    RGPetController p;
    int hp = 100;
    EXPECT_FALSE(p.ReplyingHP(hp, 100, 1000.0F));
    EXPECT_EQ(hp, 100);
    EXPECT_FLOAT_EQ(p.ReplyTimer(), 0.0F); // gated out: timer untouched
}

TEST(RGPetControllerTest, ReplyingHPAccumulatesTimerAndHealsAtInterval) {
    // Interval = reply_time2 + reply_time1 = 2 + 4 = 6.  heal = max_hp / 5.
    RGPetController p;
    int hp = 10;
    const int maxHp = 100;
    // Drive with fixed dt = 1.0; the timer crosses 6.0 on the 6th tick.
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(p.ReplyingHP(hp, maxHp, 1.0F));
        EXPECT_EQ(hp, 10);
    }
    EXPECT_FLOAT_EQ(p.ReplyTimer(), 5.0F);
    EXPECT_TRUE(p.ReplyingHP(hp, maxHp, 1.0F)); // timer hits 6.0 -> heal
    EXPECT_EQ(hp, 10 + 100 / 5);                // hp += max_hp/5 = +20 -> 30
    EXPECT_FLOAT_EQ(p.ReplyTimer(), RGPetController::kReplyTime1); // reset to 4.0
}

TEST(RGPetControllerTest, ReplyingHPHealUsesIntegerDivision) {
    // hp += max_hp / 5 is INTEGER division (line 427344). max_hp = 12 -> +2.
    RGPetController p;
    int hp = 0;
    const int maxHp = 12;
    // Arm the timer just past the interval in one tick.
    EXPECT_TRUE(p.ReplyingHP(hp, maxHp, RGPetController::kReplyTime1 +
                                            RGPetController::kReplyTime2));
    EXPECT_EQ(hp, 12 / 5); // 12/5 == 2 (integer)
}

TEST(RGPetControllerTest, ReplyingHPClampsToMaxHp) {
    // After healing, hp is clamped to max_hp (lines 427351-427354).
    RGPetController p;
    int hp = 95;
    const int maxHp = 100; // +20 would overshoot to 115 -> clamp to 100
    EXPECT_TRUE(p.ReplyingHP(hp, maxHp, RGPetController::kReplyTime1 +
                                            RGPetController::kReplyTime2));
    EXPECT_EQ(hp, 100);
}

TEST(RGPetControllerTest, ReplyingHPTimerResetsToReplyTime1NotZero) {
    // The reset value is reply_time1 (0x5c), NOT zero (line 427345). With the timer
    // re-armed to 4.0, only 2.0 more is needed to reach the 6.0 interval again.
    RGPetController p;
    int hp = 0;
    const int maxHp = 100;
    EXPECT_TRUE(p.ReplyingHP(hp, maxHp, 6.0F)); // first heal: hp 0 -> 20
    EXPECT_EQ(hp, 20);
    EXPECT_FLOAT_EQ(p.ReplyTimer(), 4.0F);      // reset to reply_time1
    // Now only 2.0 more reaches 6.0 again (proving reset != 0).
    EXPECT_FALSE(p.ReplyingHP(hp, maxHp, 1.0F)); // timer 5.0, no heal
    EXPECT_EQ(hp, 20);
    EXPECT_TRUE(p.ReplyingHP(hp, maxHp, 1.0F));  // timer 6.0 -> heal again
    EXPECT_EQ(hp, 40);
}

// ---- TurnTo: Vector2.Reflect ----------------------------------------------

TEST(RGPetControllerTest, TurnToReflectsOffAxisAlignedNormal) {
    // Reflect(v, n) = v - 2*dot(v,n)*n. Bounce (1,-1) off the floor normal (0,1).
    RGPetController p;
    p.SetMoveDirection({1.0F, -1.0F});
    p.TurnTo({0.0F, 1.0F});
    // dot((1,-1),(0,1)) = -1; v - 2*(-1)*(0,1) = (1,-1)+(0,2) = (1,1)
    ExpectVec2Near(p.MoveDirection(), glm::vec2(1.0F, 1.0F));
}

TEST(RGPetControllerTest, TurnToReflectsOffVerticalWall) {
    // Bounce (1,2) off a wall normal (-1,0): x flips, y preserved.
    RGPetController p;
    p.SetMoveDirection({1.0F, 2.0F});
    p.TurnTo({-1.0F, 0.0F});
    // dot((1,2),(-1,0)) = -1; v - 2*(-1)*(-1,0) = (1,2) - (2,0) = (-1,2)
    ExpectVec2Near(p.MoveDirection(), glm::vec2(-1.0F, 2.0F));
}

TEST(RGPetControllerTest, TurnToMatchesReflectFormulaArbitraryNormal) {
    // Cross-check against the closed form for a non-axis normal.
    RGPetController p;
    const glm::vec2 v(3.0F, 4.0F);
    const glm::vec2 n = glm::normalize(glm::vec2(1.0F, 1.0F));
    p.SetMoveDirection(v);
    p.TurnTo(n);
    const glm::vec2 expected = v - 2.0F * glm::dot(v, n) * n;
    ExpectVec2Near(p.MoveDirection(), expected);
}

// ---- RNG: zero-draw module -------------------------------------------------

TEST(RGPetControllerTest, BaseBodiesTakeNoRngDraw) {
    // NONE of FixedUpdate / ReplyingHP / TurnTo draws from rg_random. The seeded
    // stream must be byte-for-byte non-advancing through all of them: a parallel
    // reference stream from the same seed must still be in lockstep afterwards.
    RGPetController p;
    Game::RGRandom ref;
    p.SetSeed(777);
    ref.SetRandomSeed(777);

    MoveBranch branch = MoveBranch::FOLLOW;
    p.SetDecel(0.0F);
    p.SetMoveDirection({1.0F, 1.0F});
    p.FixedUpdate(2.0F, 0.0F, branch); // FOLLOW: no draw
    p.SetDecel(5.0F);
    p.SetDamping(0.5F);
    p.SetForceDirection({1.0F, 0.0F});
    p.FixedUpdate(0.0F, 0.0F, branch); // COAST: no draw

    int hp = 10;
    p.ReplyingHP(hp, 100, 6.0F);       // heal: no draw
    p.TurnTo({0.0F, 1.0F});            // reflect: no draw

    // The pet's stream has not advanced; it must match the untouched reference.
    EXPECT_EQ(p.Rng().Range(0, 1000), ref.Range(0, 1000));
    EXPECT_EQ(p.Rng().Range(0, 1000), ref.Range(0, 1000));
}

TEST(RGPetControllerTest, SeededReportsSeeded) {
    RGPetController p;
    EXPECT_FALSE(p.Seeded());
    p.SetSeed(42);
    EXPECT_TRUE(p.Seeded());
}

// NOLINTEND(readability-magic-numbers)
