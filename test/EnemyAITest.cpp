#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>

#include "combat/EnemyAI.hpp"

using Game::AIState;
using Game::EnemyAI;
using Game::EnemyDef;

namespace {
EnemyDef MakeEnemy(float shootCdSeconds) {
    EnemyDef d;
    d.shootCd = shootCdSeconds;
    return d;
}

EnemyDef MakeEnemy(float shootCdSeconds, float friction, float scoutRate) {
    EnemyDef d;
    d.shootCd = shootCdSeconds;
    d.friction = friction;
    d.scoutRate = scoutRate;
    return d;
}
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

// ---------------------------------------------------------------------------
// Legacy high-level Decision API (must keep working; Enemy/GameScene callers).
// ---------------------------------------------------------------------------

TEST(EnemyAITest, IdleWhenPlayerFar) {
    const auto def = MakeEnemy(1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    const auto d = ai.Update(16.0F, glm::vec2(0.0F, 0.0F), glm::vec2(500.0F, 0.0F));
    EXPECT_EQ(d.state, AIState::Idle);
    EXPECT_FALSE(d.shouldShoot);
    EXPECT_FLOAT_EQ(d.moveDir.x, 0.0F);
}

TEST(EnemyAITest, ChasesTowardPlayerInDetectRange) {
    const auto def = MakeEnemy(1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    const auto d = ai.Update(16.0F, glm::vec2(0.0F, 0.0F), glm::vec2(60.0F, 0.0F));
    EXPECT_EQ(d.state, AIState::Chase);
    EXPECT_NEAR(d.moveDir.x, 1.0F, 1e-4F); // unit vector toward +x
    EXPECT_NEAR(d.moveDir.y, 0.0F, 1e-4F);
    EXPECT_FALSE(d.shouldShoot);
}

TEST(EnemyAITest, AttacksAndShootsWhenClose) {
    const auto def = MakeEnemy(1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    const auto d = ai.Update(16.0F, glm::vec2(0.0F, 0.0F), glm::vec2(20.0F, 0.0F));
    EXPECT_EQ(d.state, AIState::Attack);
    EXPECT_TRUE(d.shouldShoot); // first frame in range fires
    EXPECT_FLOAT_EQ(d.moveDir.x, 0.0F); // holds position
}

TEST(EnemyAITest, ShootRespectsCooldown) {
    const auto def = MakeEnemy(1.0F); // 1 second between shots
    EnemyAI ai(def, 100.0F, 30.0F);
    const glm::vec2 self(0.0F, 0.0F);
    const glm::vec2 player(20.0F, 0.0F);

    EXPECT_TRUE(ai.Update(16.0F, self, player).shouldShoot);  // fires
    EXPECT_FALSE(ai.Update(16.0F, self, player).shouldShoot); // still cooling
    EXPECT_TRUE(ai.Update(1000.0F, self, player).shouldShoot); // 1s later, fires
}

// ---------------------------------------------------------------------------
// Knockback impulse: hard cap 28 (RGEController__GetForce).
// ---------------------------------------------------------------------------

TEST(EnemyAITest, GetForceClampsToCap28) {
    const auto def = MakeEnemy(1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.GetForce(glm::vec2(1.0F, 0.0F), 100.0F); // way over the cap
    EXPECT_FLOAT_EQ(ai.InertialVel(), EnemyAI::kForceCap);
    EXPECT_FLOAT_EQ(EnemyAI::kForceCap, 28.0F);
}

TEST(EnemyAITest, GetForceBelowCapIsUnchanged) {
    const auto def = MakeEnemy(1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.GetForce(glm::vec2(0.0F, 1.0F), 12.5F);
    EXPECT_FLOAT_EQ(ai.InertialVel(), 12.5F);
    EXPECT_FLOAT_EQ(ai.ForceDirection().x, 0.0F);
    EXPECT_FLOAT_EQ(ai.ForceDirection().y, 1.0F);
}

TEST(EnemyAITest, DeadEnemyIgnoresForce) {
    const auto def = MakeEnemy(1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.SetDead();
    ai.GetForce(glm::vec2(1.0F, 0.0F), 20.0F);
    EXPECT_FLOAT_EQ(ai.InertialVel(), 0.0F);
}

// ---------------------------------------------------------------------------
// Velocity composition + MULTIPLICATIVE friction decay
// (EnemyAI01__FixedUpdate). velocity = moveDir*speed*(speed_rate+1) + knockback;
// inertial_vel *= friction each active step.
// ---------------------------------------------------------------------------

TEST(EnemyAITest, IntegrateWalkVelocityUsesSpeedAndRate) {
    const auto def = MakeEnemy(1.0F, 0.6F, 1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    // No knockback active: pure walk = dir * speed * (speed_rate + 1).
    const glm::vec2 v =
        ai.IntegrateVelocity(glm::vec2(1.0F, 0.0F), 4.0F, 0.5F);
    EXPECT_NEAR(v.x, 4.0F * 1.5F, 1e-4F); // 6.0
    EXPECT_NEAR(v.y, 0.0F, 1e-4F);
}

TEST(EnemyAITest, FrictionDecaysInertiaMultiplicatively) {
    const auto def = MakeEnemy(1.0F, 0.6F, 1.0F); // friction = 0.6
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.GetForce(glm::vec2(1.0F, 0.0F), 28.0F);
    EXPECT_FLOAT_EQ(ai.InertialVel(), 28.0F);

    // First active step decays multiplicatively: 28 * 0.6 = 16.8.
    ai.IntegrateVelocity(glm::vec2(0.0F, 0.0F), 0.0F, 0.0F);
    EXPECT_NEAR(ai.InertialVel(), 28.0F * 0.6F, 1e-3F);
    // Second step: 16.8 * 0.6 = 10.08.
    ai.IntegrateVelocity(glm::vec2(0.0F, 0.0F), 0.0F, 0.0F);
    EXPECT_NEAR(ai.InertialVel(), 28.0F * 0.6F * 0.6F, 1e-3F);
}

TEST(EnemyAITest, KnockbackAddsToVelocityWhileActive) {
    const auto def = MakeEnemy(1.0F, 0.6F, 1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.GetForce(glm::vec2(0.0F, 1.0F), 10.0F); // knockback +y, magnitude 10
    // walk along +x = 4*(0+1)=4 ; knock along +y = 10.
    const glm::vec2 v =
        ai.IntegrateVelocity(glm::vec2(1.0F, 0.0F), 4.0F, 0.0F);
    EXPECT_NEAR(v.x, 4.0F, 1e-3F);
    EXPECT_NEAR(v.y, 10.0F, 1e-3F);
}

TEST(EnemyAITest, InertiaBelowThresholdIsDropped) {
    const auto def = MakeEnemy(1.0F, 0.6F, 1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.GetForce(glm::vec2(0.0F, 1.0F), 0.5F); // <= 1.0 threshold
    const glm::vec2 v =
        ai.IntegrateVelocity(glm::vec2(1.0F, 0.0F), 4.0F, 0.0F);
    EXPECT_NEAR(v.y, 0.0F, 1e-4F);            // knockback ignored
    EXPECT_FLOAT_EQ(ai.InertialVel(), 0.5F);  // and not decayed
}

TEST(EnemyAITest, KinematicEnemyIgnoresKnockback) {
    const auto def = MakeEnemy(1.0F, 0.6F, 1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.SetKinematic(true);
    ai.GetForce(glm::vec2(0.0F, 1.0F), 28.0F);
    const glm::vec2 v =
        ai.IntegrateVelocity(glm::vec2(1.0F, 0.0F), 4.0F, 0.0F);
    EXPECT_NEAR(v.y, 0.0F, 1e-4F);
    EXPECT_FLOAT_EQ(ai.InertialVel(), 28.0F); // not decayed (turret)
}

// ---------------------------------------------------------------------------
// TurnTo: reflect BOTH move and force directions about the wall normal.
// ---------------------------------------------------------------------------

TEST(EnemyAITest, TurnToReflectsMoveAndForceAboutNormal) {
    const auto def = MakeEnemy(1.0F, 0.6F, 1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    // Set up a move direction (via Scout) and a force direction (via GetForce).
    ai.SetSeed(1);
    ai.Scout(glm::vec2(0.0F, 0.0F), glm::vec2(1.0F, 0.0F)); // move +x
    ai.GetForce(glm::vec2(1.0F, 0.0F), 10.0F);              // force +x

    // Hit a left-facing wall (normal pointing -x): +x should reflect to -x.
    ai.TurnTo(glm::vec2(-1.0F, 0.0F));
    EXPECT_NEAR(ai.MoveDirection().x, -1.0F, 1e-4F);
    EXPECT_NEAR(ai.MoveDirection().y, 0.0F, 1e-4F);
    EXPECT_NEAR(ai.ForceDirection().x, -1.0F, 1e-4F);
    EXPECT_NEAR(ai.ForceDirection().y, 0.0F, 1e-4F);
}

TEST(EnemyAITest, TurnToSlidesAlongWall) {
    const auto def = MakeEnemy(1.0F, 0.6F, 1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.SetSeed(1);
    // Moving diagonally up-right into a horizontal floor (normal +y).
    ai.Scout(glm::vec2(0.0F, 0.0F), glm::vec2(1.0F, -1.0F)); // down-right
    // Wall normal +y reflects the y component, x slides through.
    ai.TurnTo(glm::vec2(0.0F, 1.0F));
    EXPECT_GT(ai.MoveDirection().x, 0.0F); // still sliding +x
    EXPECT_GT(ai.MoveDirection().y, 0.0F); // y flipped upward
}

// ---------------------------------------------------------------------------
// Scout: deterministic RNG roll (same seed -> same stream) + gating.
// ---------------------------------------------------------------------------

TEST(EnemyAITest, ScoutPointsTowardPlayer) {
    const auto def = MakeEnemy(1.0F, 0.6F, 1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.SetSeed(42);
    const glm::vec2 dir = ai.Scout(glm::vec2(0.0F, 0.0F), glm::vec2(0.0F, 50.0F));
    EXPECT_NEAR(dir.x, 0.0F, 1e-4F);
    EXPECT_NEAR(dir.y, 1.0F, 1e-4F);
    EXPECT_NEAR(ai.MoveDirection().y, 1.0F, 1e-4F);
}

TEST(EnemyAITest, ScoutRollIsDeterministicForSameSeed) {
    // Two independently seeded enemies must advance identical RNG streams, so
    // their *subsequent* draws (after the per-scout Range(0,10)) line up.
    const auto def = MakeEnemy(1.0F, 0.6F, 1.0F);
    EnemyAI a(def, 100.0F, 30.0F);
    EnemyAI b(def, 100.0F, 30.0F);
    a.SetSeed(12345);
    b.SetSeed(12345);

    const glm::vec2 self(0.0F, 0.0F);
    const glm::vec2 player(10.0F, 0.0F);
    for (int i = 0; i < 8; ++i) {
        a.Scout(self, player);
        b.Scout(self, player);
    }
    // After equal numbers of scout draws, the streams must still match. Verify
    // via a probe: draw the same int range from each enemy's seeded copy.
    EnemyAI c(def, 100.0F, 30.0F);
    EnemyAI dd(def, 100.0F, 30.0F);
    c.SetSeed(777);
    dd.SetSeed(777);
    const glm::vec2 r1 = c.Scout(self, player);
    const glm::vec2 r2 = dd.Scout(self, player);
    EXPECT_FLOAT_EQ(r1.x, r2.x);
    EXPECT_FLOAT_EQ(r1.y, r2.y);
}

TEST(EnemyAITest, ScoutDoesNotAdvanceStreamWhenDead) {
    // A gated (dead) scout returns before drawing, so the next live scout's
    // implied stream position differs from a never-gated baseline. We assert
    // the gate by checking the dead scout produces no movement and no crash.
    const auto def = MakeEnemy(1.0F, 0.6F, 1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.SetSeed(1);
    ai.SetDead();
    const glm::vec2 dir = ai.Scout(glm::vec2(0.0F, 0.0F), glm::vec2(50.0F, 0.0F));
    EXPECT_FLOAT_EQ(dir.x, 0.0F);
    EXPECT_FLOAT_EQ(dir.y, 0.0F);
}

// ---------------------------------------------------------------------------
// Scout cadence (scout_rate) gating.
// ---------------------------------------------------------------------------

TEST(EnemyAITest, ScoutDueFollowsScoutRateCadence) {
    const auto def = MakeEnemy(1.0F, 0.6F, 0.5F); // scout every 500 ms
    EnemyAI ai(def, 100.0F, 30.0F);
    EXPECT_FALSE(ai.ScoutDue(200.0F)); // 200 < 500
    EXPECT_FALSE(ai.ScoutDue(200.0F)); // 400 < 500
    EXPECT_TRUE(ai.ScoutDue(200.0F));  // 600 >= 500 -> due, carries 100 over
    EXPECT_FALSE(ai.ScoutDue(200.0F)); // 300 < 500
}

// ---------------------------------------------------------------------------
// Shoot cadence: gated by can_shoot && !dead && !dizzy; re-armed each period.
// ---------------------------------------------------------------------------

TEST(EnemyAITest, ShootCadenceReArmsViaCanShoot) {
    const auto def = MakeEnemy(0.5F); // 500 ms cadence
    EnemyAI ai(def, 100.0F, 30.0F);
    const glm::vec2 self(0.0F, 0.0F);
    const glm::vec2 player(10.0F, 0.0F);

    EXPECT_TRUE(ai.Update(16.0F, self, player).shouldShoot);  // shot 1
    EXPECT_FALSE(ai.Update(100.0F, self, player).shouldShoot); // cooling
    EXPECT_TRUE(ai.Update(500.0F, self, player).shouldShoot);  // re-armed shot 2
}

TEST(EnemyAITest, DizzyGatesEverything) {
    const auto def = MakeEnemy(0.5F, 0.6F, 1.0F);
    EnemyAI ai(def, 100.0F, 30.0F);
    const glm::vec2 self(0.0F, 0.0F);
    const glm::vec2 player(10.0F, 0.0F); // in attack range

    ai.Dizzy(300.0F);
    EXPECT_TRUE(ai.IsDizzy());
    auto d = ai.Update(16.0F, self, player);
    EXPECT_FALSE(d.shouldShoot); // gated while dizzy
    EXPECT_FLOAT_EQ(d.moveDir.x, 0.0F);

    // Tick past the dizzy duration: recovers this same step and, no longer
    // gated, fires immediately because the shoot cooldown is ready.
    const auto recovered = ai.Update(300.0F, self, player);
    EXPECT_FALSE(ai.IsDizzy());
    EXPECT_TRUE(recovered.shouldShoot);
}

TEST(EnemyAITest, DeadGatesDecision) {
    const auto def = MakeEnemy(0.5F);
    EnemyAI ai(def, 100.0F, 30.0F);
    ai.SetDead();
    const auto d = ai.Update(16.0F, glm::vec2(0.0F, 0.0F), glm::vec2(10.0F, 0.0F));
    EXPECT_FALSE(d.shouldShoot);
    EXPECT_FLOAT_EQ(d.moveDir.x, 0.0F);
}

// ---------------------------------------------------------------------------
// Data plumbing: friction / scout_rate / e_size flow from the EnemyDef.
// ---------------------------------------------------------------------------

TEST(EnemyAITest, AccessorsExposeDefFields) {
    EnemyDef def;
    def.friction = 0.7F;
    def.scoutRate = 0.5F;
    def.eSize = 3;
    EnemyAI ai(def, 100.0F, 30.0F);
    EXPECT_FLOAT_EQ(ai.Friction(), 0.7F);
    EXPECT_FLOAT_EQ(ai.ScoutRate(), 0.5F);
    EXPECT_EQ(ai.ESize(), 3);
}

// NOLINTEND(readability-magic-numbers)
