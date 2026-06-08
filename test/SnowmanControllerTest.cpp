#include <gtest/gtest.h>

#include <vector>

#include "combat/SnowmanController.hpp"

using Game::SnowmanController;
using StateChange = SnowmanController::StateChange;

// NOLINTBEGIN(readability-magic-numbers)

// ---- ShootReflection ------------------------------------------------------

TEST(SnowmanControllerTest, ShootReflectionClearsReArmFlagWhenAlive) {
    SnowmanController s;
    EXPECT_TRUE(s.ReArmFlag()); // ctor init = true (RGPetController__ctor 0x71=1)
    EXPECT_TRUE(s.ShootReflection());
    EXPECT_FALSE(s.ReArmFlag()); // byte 0x71 = 0 (line 1652679)
}

TEST(SnowmanControllerTest, ShootReflectionGatedWhenDestroyed) {
    SnowmanController s;
    s.SetDestroyed(true);
    EXPECT_FALSE(s.ShootReflection()); // 0x0d gate -> early return, no write
    EXPECT_TRUE(s.ReArmFlag());         // re-arm flag untouched on the gated path
}

// ---- Scout ----------------------------------------------------------------

TEST(SnowmanControllerTest, ScoutLinksTargetFromMaster) {
    SnowmanController s;
    EXPECT_FALSE(s.HasTarget());
    EXPECT_TRUE(s.Scout(true));   // not paused -> cadence dispatch would run
    EXPECT_TRUE(s.HasTarget());   // target(0x1c) = master(0x74)
    EXPECT_TRUE(s.HasMaster());
}

TEST(SnowmanControllerTest, ScoutWithNoMasterLinksNullTarget) {
    SnowmanController s;
    EXPECT_TRUE(s.Scout(false)); // not paused
    EXPECT_FALSE(s.HasTarget()); // target(0x1c) = master(0x74) == null
    EXPECT_FALSE(s.HasMaster());
}

TEST(SnowmanControllerTest, ScoutGatedWhilePaused) {
    SnowmanController s;
    s.SetPaused(true);
    // The target copy happens before the paused gate; paused only blocks the
    // downstream cadence dispatch (return false).
    EXPECT_FALSE(s.Scout(true)); // paused -> no cadence dispatch
    EXPECT_TRUE(s.HasTarget());  // target still linked from master
}

// ---- GetHurt --------------------------------------------------------------

TEST(SnowmanControllerTest, GetHurtRoutedWhenAlive) {
    SnowmanController s;
    EXPECT_TRUE(s.GetHurt()); // 0x0d == 0 -> routes to UICanvas
}

TEST(SnowmanControllerTest, GetHurtIgnoredWhenDestroyed) {
    SnowmanController s;
    s.SetDestroyed(true);
    EXPECT_FALSE(s.GetHurt()); // 0x0d gate -> ignored
}

// ---- OnGameStateChange ----------------------------------------------------

TEST(SnowmanControllerTest, OnGameStateIgnoredWhenInactive) {
    SnowmanController s; // active defaults false (0x0c == 0)
    float petSpeedRate = 1.0F; // this pet's own role_attribute.speed_rate
    EXPECT_EQ(s.OnGameStateChange(SnowmanController::kStateResume, petSpeedRate),
              StateChange::Ignored);
    EXPECT_FLOAT_EQ(petSpeedRate, 1.0F); // no write at all on the inactive path
    EXPECT_FALSE(s.Paused());            // paused untouched
}

TEST(SnowmanControllerTest, OnGameStateResumeClearsPausedAndBoostsSpeed) {
    // The boosted speed_rate is the pet's OWN role_attribute.speed_rate
    // (controller field 0x40 + 0x14), not the master's. The decomp reads
    // p+0x40 from param_1 (the controller itself) at line 1652890.
    SnowmanController s;
    s.SetActive(true);
    s.SetPaused(true);
    float petSpeedRate = 2.0F; // this pet's own role_attribute.speed_rate
    EXPECT_EQ(s.OnGameStateChange(SnowmanController::kStateResume, petSpeedRate),
              StateChange::Resumed);
    EXPECT_FALSE(s.Paused());            // 0x44 = 0 (line 1652888)
    EXPECT_FLOAT_EQ(petSpeedRate, 2.5F); // pet speed_rate += 0.5 (line 1652894)
}

TEST(SnowmanControllerTest, OnGameStatePauseSetsPaused) {
    SnowmanController s;
    s.SetActive(true);
    float petSpeedRate = 3.0F; // this pet's own role_attribute.speed_rate
    EXPECT_EQ(s.OnGameStateChange(SnowmanController::kStatePause, petSpeedRate),
              StateChange::Paused);
    EXPECT_TRUE(s.Paused());             // 0x44 = 1 (line 1652897)
    EXPECT_FLOAT_EQ(petSpeedRate, 3.0F); // pause path does NOT touch speed_rate
}

TEST(SnowmanControllerTest, OnGameStateOtherIsNoChange) {
    SnowmanController s;
    s.SetActive(true);
    s.SetPaused(true);
    float petSpeedRate = 1.0F; // this pet's own role_attribute.speed_rate
    EXPECT_EQ(s.OnGameStateChange(0, petSpeedRate), StateChange::NoChange);
    EXPECT_TRUE(s.Paused());             // unchanged
    EXPECT_FLOAT_EQ(petSpeedRate, 1.0F); // unchanged
    EXPECT_EQ(s.OnGameStateChange(3, petSpeedRate), StateChange::NoChange);
    EXPECT_FLOAT_EQ(petSpeedRate, 1.0F);
}

TEST(SnowmanControllerTest, ResumeBoostIsCumulativeAcrossCalls) {
    // Each resume adds +0.5 to the pet's OWN role_attribute.speed_rate
    // (controller field 0x40 + 0x14); faithful to the additive write at
    // line 1652894 (the master at 0x74 is never touched).
    SnowmanController s;
    s.SetActive(true);
    float petSpeedRate = 1.0F; // this pet's own role_attribute.speed_rate
    s.OnGameStateChange(SnowmanController::kStateResume, petSpeedRate);
    s.OnGameStateChange(SnowmanController::kStateResume, petSpeedRate);
    EXPECT_FLOAT_EQ(petSpeedRate, 2.0F); // 1.0 + 0.5 + 0.5
}

// ---- Determinism: the Snowman takes ZERO rng draws ------------------------

TEST(SnowmanControllerTest, NoMethodConsumesTheRngStream) {
    // Snowman has no rg_random draws anywhere; exercising every brain method
    // must leave the stream byte-identical to one that was never touched.
    SnowmanController exercised;
    Game::RGRandom reference;
    exercised.SetSeed(2024);
    reference.SetRandomSeed(2024);

    exercised.SetActive(true);
    float petSpeedRate = 1.0F; // this pet's own role_attribute.speed_rate
    for (int i = 0; i < 16; ++i) {
        exercised.ShootReflection();
        exercised.Scout(true);
        exercised.GetHurt();
        exercised.OnGameStateChange(SnowmanController::kStatePause, petSpeedRate);
        exercised.OnGameStateChange(SnowmanController::kStateResume, petSpeedRate);
    }

    // Both streams must still produce the identical next sequence.
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(exercised.Rng().Range(0, 1000), reference.Range(0, 1000));
    }
}

TEST(SnowmanControllerTest, GateBranchesAreDeterministic) {
    // Same inputs -> same StateChange outcome, every time (no hidden RNG).
    auto run = [] {
        SnowmanController s;
        s.SetActive(true);
        float sr = 0.0F;
        std::vector<int> trace;
        const int states[] = {0, 1, 2, 3, 2, 1};
        for (int st : states) {
            trace.push_back(static_cast<int>(s.OnGameStateChange(st, sr)));
        }
        return trace;
    };
    EXPECT_EQ(run(), run());
}

// NOLINTEND(readability-magic-numbers)
