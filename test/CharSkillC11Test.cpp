#include <gtest/gtest.h>

#include <vector>

#include "combat/CharSkillC11.hpp"
#include "data/GameData.hpp"
#include "sim/SimConfig.hpp"

using Game::CharSkillC11;
using Game::HolyLightZone;
using Game::Sim::kFixedStepMs;
using Game::Sim::kFixedStepSeconds;

// P3.3.c C11 (Priest) brain. Expected numbers: characters.json (skill_cd 14, atk 3) and the
// decompile constants cited in CharSkillC11.hpp (typed 3521-3601; DISASM for the spawn
// tail and the BulletGreenLight pulse timings).

// NOLINTBEGIN(readability-magic-numbers)

namespace {
const Game::CharacterDef &Hero() {
    static Game::GameData gd;
    static const bool ok = gd.LoadAll(RESOURCE_DIR);
    EXPECT_TRUE(ok);
    const Game::CharacterDef *c = gd.FindCharacter("c11");
    EXPECT_NE(c, nullptr);
    static const Game::CharacterDef fallback{};
    return c != nullptr ? *c : fallback;
}

void TickSteps(CharSkillC11 &c, int n) {
    for (int i = 0; i < n; ++i) {
        c.Tick(kFixedStepMs);
    }
}
} // namespace

TEST(CharSkillC11Test, DataRowIsThePriest) {
    EXPECT_FLOAT_EQ(Hero().skillCd, 14.0F); // characters.json c11 skill_cd
    EXPECT_EQ(Hero().SkillRefCount("skill_obj"), 1U); // c11.prefab skill_obj -> Holy_light_c11
    EXPECT_EQ(Hero().skillRefs.at("skill_obj").front(), "Holy_light_c11");
}

// --- activation gate (TYPED 3552-3580) -----------------------------------------------

TEST(CharSkillC11Test, GateIsSkillReadyAndTheLightSpawnsOnce) {
    CharSkillC11 c(Hero().skillCd);
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.ConsumeSpawn()); // the holy light object spawns
    EXPECT_FALSE(c.ConsumeSpawn()); // one-shot
    EXPECT_FALSE(c.TryActivateSkill()); // on cooldown now
    EXPECT_FALSE(c.ConsumeSpawn());
}

TEST(CharSkillC11Test, TheUltimateIsInstantInSkillNeverObservableAfterTheCall) {
    // DISASM 0x26af90: vcall slot 25 (RoleSkillEnd) at the tail of the same RoleSkill call.
    CharSkillC11 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.InSkill());
}

// --- end / reset + cooldown at the fixed step (TYPED 3521-3530, 3587-3601) ---------------

TEST(CharSkillC11Test, CooldownRestartsAtTheCastAndCountsUpEveryFixedStep) {
    CharSkillC11 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 14.0F);
    TickSteps(c, 350); // 7.0 s
    EXPECT_NEAR(c.CooldownRemaining(), 14.0F - 350 * kFixedStepSeconds, 1e-2F); // 7.0
    EXPECT_FALSE(c.SkillReady());
    TickSteps(c, 360); // past 14 s
    EXPECT_TRUE(c.SkillReady());
}

TEST(CharSkillC11Test, EndSkillIsUnconditional) {
    CharSkillC11 c(Hero().skillCd);
    c.EndSkill(); // TYPED 3587-3601: no guard
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 14.0F);
}

TEST(CharSkillC11Test, NegativeCooldownClampsAndNoRngIsDrawn) {
    CharSkillC11 neg(-1.0F);
    EXPECT_FLOAT_EQ(neg.SkillCd(), 0.0F);
    CharSkillC11 a(Hero().skillCd);
    CharSkillC11 b(Hero().skillCd);
    a.SetSeed(8);
    b.SetSeed(8);
    for (int i = 0; i < 3; ++i) {
        a.TryActivateSkill();
        TickSteps(a, 720);
    }
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

// --- the BulletGreenLight pulses (DISASM 0xafa2e4-0xafaacc) --------------------------------

namespace {
// Run a zone for @p calls fixed steps with the player at @p player; returns the heal per call.
std::vector<int> RunZone(HolyLightZone &z, glm::vec2 player, int calls) {
    std::vector<int> heals;
    for (int i = 0; i < calls; ++i) {
        heals.push_back(z.Step(kFixedStepSeconds, player));
    }
    return heals;
}
} // namespace

TEST(CharSkillC11Test, ThreePulsesLandAtSpawnPlusZeroPointEightAndOnePointSixSeconds) {
    HolyLightZone z;
    z.Start({0.0F, 0.0F}, /*buffDigestive=*/false);
    const std::vector<int> heals = RunZone(z, {0.0F, 0.0F}, 200);
    int total = 0;
    std::vector<int> at;
    for (std::size_t i = 0; i < heals.size(); ++i) {
        total += heals[i];
        if (heals[i] > 0) {
            at.push_back(static_cast<int>(i) + 1); // 1-based call number
            EXPECT_EQ(heals[i], 1); // RestoreHealth(1) (buff_digestive off)
        }
    }
    // Pulse 1 on the first FixedUpdate (can_hit starts true), pulse 2 when the 0.8 s
    // Invoke re-arms can_hit (40 calls of 20 ms), pulse 3 another 40 calls later; GasEnd
    // fires at 1.8 s = call 90 and stops the 4th (would-be call 120).
    EXPECT_EQ(at, (std::vector<int>{1, 40, 80}));
    EXPECT_EQ(total, 3);
    EXPECT_EQ(z.Pulses(), 3);
}

TEST(CharSkillC11Test, TheLightIsDestroyedAfterFourAndAHalfSeconds) {
    HolyLightZone z;
    z.Start({0.0F, 0.0F}, false);
    RunZone(z, {0.0F, 0.0F}, 224); // 4.48 s
    EXPECT_TRUE(z.Alive());
    z.Step(kFixedStepSeconds, {0.0F, 0.0F}); // call 225 = 4.5 s
    EXPECT_FALSE(z.Alive());
}

TEST(CharSkillC11Test, OnlyAPlayerWithinFourUnitsIsHealed) {
    HolyLightZone inside;
    inside.Start({0.0F, 0.0F}, false);
    EXPECT_EQ(inside.Step(kFixedStepSeconds, {4.0F, 0.0F}), 1); // exactly on the radius
    HolyLightZone outside;
    outside.Start({0.0F, 0.0F}, false);
    EXPECT_EQ(outside.Step(kFixedStepSeconds, {4.01F, 0.0F}), 0);
    EXPECT_EQ(outside.Pulses(), 1); // the pulse still happened, nobody was in range
    // The player walks out after the first pulse: pulses 2 and 3 heal nothing.
    HolyLightZone walker;
    walker.Start({0.0F, 0.0F}, false);
    int total = walker.Step(kFixedStepSeconds, {0.0F, 0.0F});
    const std::vector<int> rest = RunZone(walker, {9.0F, 0.0F}, 150);
    for (int h : rest) {
        total += h;
    }
    EXPECT_EQ(total, 1);
    EXPECT_EQ(walker.Pulses(), 3);
}

TEST(CharSkillC11Test, TheDigestiveRunBuffDoublesEachPulse) {
    HolyLightZone z;
    z.Start({0.0F, 0.0F}, /*buffDigestive=*/true); // RGGameProcess.buff_digestive (+0x47)
    const std::vector<int> heals = RunZone(z, {0.0F, 0.0F}, 100);
    int total = 0;
    for (int h : heals) {
        total += h;
    }
    EXPECT_EQ(total, 6); // 3 pulses x 2
}

// NOLINTEND(readability-magic-numbers)
