#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "combat/CharSkillC07.hpp"
#include "data/GameData.hpp"
#include "data/RGRandom.hpp"
#include "sim/SimConfig.hpp"

using Game::BatSwarm;
using Game::CharSkillC07;
using Game::SkillTarget;
using Game::Sim::kFixedStepMs;
using Game::Sim::kFixedStepSeconds;

// P3.3.c C07 (Vampire) brain + bat swarm. Expected numbers: characters.json (skill_cd 8,
// atk 3; BulletBat.damage 5 from the bullet_bat prefab via skill_params) and the decompile
// constants cited in CharSkillC07.hpp (typed 1881-2118; DISASM for the spawn loop, the
// has_hit branch and the BulletBat state machine).

// NOLINTBEGIN(readability-magic-numbers)

namespace {
const Game::CharacterDef &Hero() {
    static Game::GameData gd;
    static const bool ok = gd.LoadAll(RESOURCE_DIR);
    EXPECT_TRUE(ok);
    const Game::CharacterDef *c = gd.FindCharacter("c07");
    EXPECT_NE(c, nullptr);
    static const Game::CharacterDef fallback{};
    return c != nullptr ? *c : fallback;
}

void TickSteps(CharSkillC07 &c, int n) {
    for (int i = 0; i < n; ++i) {
        c.Tick(kFixedStepMs);
    }
}
} // namespace

TEST(CharSkillC07Test, DataRowIsTheVampire) {
    EXPECT_FLOAT_EQ(Hero().skillCd, 8.0F); // characters.json c07 skill_cd
    EXPECT_EQ(Hero().atk, 3);
    EXPECT_EQ(Hero().skillRefs.at("bullet_bat").front(), "bullet_bat");
    // The prefab OVERRIDES the BulletBat ctor default (2, TYPED 89494-89500) with 5.
    EXPECT_FLOAT_EQ(Hero().SkillParam("BulletBat.damage", 2.0F), 5.0F);
}

// --- activation gate (TYPED 1969-1997) -----------------------------------------------------

TEST(CharSkillC07Test, GateEntersInSkillAndReleasesSixBatsOnce) {
    CharSkillC07 c(Hero().skillCd);
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill());
    EXPECT_EQ(c.ConsumeLaunch(), 6); // 2n bats, n = 3 (DISASM 0x266ae4)
    EXPECT_EQ(c.ConsumeLaunch(), 0); // one-shot
    EXPECT_EQ(c.BatCount(), 0);
    EXPECT_EQ(c.BatHitCount(), 0);
    EXPECT_FALSE(c.TryActivateSkill()); // !in_skill gate
}

TEST(CharSkillC07Test, StrengthenedReleasesTenBats) {
    CharSkillC07 c(Hero().skillCd);
    c.SetSkillStrengthen(true); // T-42: c_level > 4, not wired in the sim
    ASSERT_TRUE(c.TryActivateSkill());
    EXPECT_EQ(c.ConsumeLaunch(), 10);
}

// --- effect magnitude: BatBack (TYPED 2058-2118 + DISASM 0x2670ec-0x267264) ----------------

TEST(CharSkillC07Test, EachMissBanksOneSixthOfTheCooldown) {
    CharSkillC07 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    c.BatBack(false, 5, 6);
    EXPECT_NEAR(c.ReduceSkillCd(), 8.0F / 6.0F, 1e-5F); // skill_cd / DAT_002672d4 (6.0)
    c.BatBack(false, 5, 6);
    c.BatBack(false, 5, 6);
    EXPECT_NEAR(c.ReduceSkillCd(), 4.0F, 1e-5F);
    EXPECT_EQ(c.BatCount(), 3);
    EXPECT_TRUE(c.InSkill()); // 3 of 6 back: still active
}

TEST(CharSkillC07Test, SixMissesEndTheSkillWithTheCooldownFullyRefunded) {
    CharSkillC07 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    for (int i = 0; i < 5; ++i) {
        const CharSkillC07::BatBackResult r = c.BatBack(false, 5, 6);
        EXPECT_FALSE(r.skillEnded);
    }
    EXPECT_TRUE(c.InSkill());
    const CharSkillC07::BatBackResult last = c.BatBack(false, 5, 6);
    EXPECT_TRUE(last.skillEnded); // bat_count == 6 -> RoleSkillEnd
    EXPECT_FALSE(c.InSkill());
    // ReSetSkillReload (0) then SkillReload(6 * 8/6 = 8) clamped to skill_cd: ready at once.
    EXPECT_NEAR(c.ThisSkillTime(), 8.0F, 1e-4F);
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.ReduceSkillCd(), 0.0F);
}

TEST(CharSkillC07Test, HitsRefundNothingAndEveryThirdHitHealsOnePoint) {
    CharSkillC07 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    EXPECT_EQ(c.BatBack(true, 3, 6).healHp, 0);
    EXPECT_EQ(c.BatBack(true, 3, 6).healHp, 0);
    const CharSkillC07::BatBackResult third = c.BatBack(true, 3, 6);
    EXPECT_EQ(third.healHp, 1); // RestoreHealth(1) while hp < max_hp
    EXPECT_EQ(third.restoreEnergy, 0);
    EXPECT_EQ(c.BatHitCount(), 0); // reset after the heal (DISASM 0x267264)
    EXPECT_FLOAT_EQ(c.ReduceSkillCd(), 0.0F); // hits never bank a refund
}

TEST(CharSkillC07Test, AtFullHealthTheThirdHitRestoresFiveEnergyInstead) {
    CharSkillC07 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    c.BatBack(true, 6, 6);
    c.BatBack(true, 6, 6);
    const CharSkillC07::BatBackResult third = c.BatBack(true, 6, 6);
    EXPECT_EQ(third.healHp, 0);
    EXPECT_EQ(third.restoreEnergy, 5); // RestoreEnergy(5) when hp >= max_hp
}

TEST(CharSkillC07Test, TheVampireRunBuffHealsEverySecondHit) {
    CharSkillC07 c(Hero().skillCd);
    c.SetRunBuffs(/*vampireHp=*/true, /*energy=*/false); // RGGameProcess.buff_vampire_hp
    ASSERT_TRUE(c.TryActivateSkill());
    EXPECT_EQ(c.BatBack(true, 3, 6).healHp, 0);
    EXPECT_EQ(c.BatBack(true, 3, 6).healHp, 1); // 2nd hit (DISASM 0x267150 movwne #2)
}

TEST(CharSkillC07Test, MixedBatsRefundOnlyTheMissesAndTheCooldownTicksAtTheFixedStep) {
    CharSkillC07 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    c.BatBack(false, 5, 6);
    c.BatBack(true, 5, 6);
    c.BatBack(false, 5, 6);
    c.BatBack(true, 5, 6);
    c.BatBack(false, 5, 6);
    const CharSkillC07::BatBackResult last = c.BatBack(true, 5, 6);
    ASSERT_TRUE(last.skillEnded);
    // 3 misses * 8/6 = 4.0 refunded -> 4 s left of the 8 s cooldown.
    EXPECT_NEAR(c.CooldownRemaining(), 4.0F, 1e-4F);
    TickSteps(c, 100); // 2.0 s at the fixed step
    EXPECT_NEAR(c.CooldownRemaining(), 4.0F - 100 * kFixedStepSeconds, 1e-3F); // 2.0
}

TEST(CharSkillC07Test, StrengthenedMissesBankOneTenthAndTheSkillEndsAtTenBats) {
    CharSkillC07 c(Hero().skillCd);
    c.SetSkillStrengthen(true);
    ASSERT_TRUE(c.TryActivateSkill());
    c.BatBack(false, 5, 6);
    EXPECT_NEAR(c.ReduceSkillCd(), 0.8F, 1e-5F); // 8 / DAT_002672d8 (10.0)
    for (int i = 0; i < 8; ++i) {
        EXPECT_FALSE(c.BatBack(false, 5, 6).skillEnded);
    }
    EXPECT_TRUE(c.BatBack(false, 5, 6).skillEnded); // 10th bat
}

TEST(CharSkillC07Test, TheCooldownCountsUpEvenWhileTheBatsAreOut) {
    // TYPED 1937-1946: SkillReload runs every awake frame, in_skill or not. Drain the
    // charge (hits only -> refund 0 -> this_skill_time 0 after the end), recast is gated;
    // verify count-up by ticking a fresh brain whose cooldown was just spent.
    CharSkillC07 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    for (int i = 0; i < 6; ++i) {
        c.BatBack(true, 6, 6);
    }
    ASSERT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.ThisSkillTime(), 0.0F); // no refund
    TickSteps(c, 50); // 1.0 s
    EXPECT_NEAR(c.ThisSkillTime(), 50 * kFixedStepSeconds, 1e-3F);
}

// --- KillSomeOne (DISASM 0x266e08-0x26706c) -----------------------------------------------------

TEST(CharSkillC07Test, KillSomeOneIsInertWithoutTheRunBuffsAndDrawsNoRng) {
    CharSkillC07 a(Hero().skillCd);
    CharSkillC07 b(Hero().skillCd);
    a.SetSeed(31);
    b.SetSeed(31);
    for (int i = 0; i < 100; ++i) {
        const CharSkillC07::KillResult r = a.KillSomeOne(3, 6);
        EXPECT_EQ(r.healHp, 0);
        EXPECT_EQ(r.restoreEnergy, 0);
    }
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000)); // no draw
}

TEST(CharSkillC07Test, KillSomeOneWithTheVampireBuffHealsOnAnEightPercentRoll) {
    CharSkillC07 c(Hero().skillCd);
    c.SetRunBuffs(true, false);
    c.SetSeed(2024);
    Game::RGRandom ref;
    ref.SetRandomSeed(2024);
    int expected = 0;
    int got = 0;
    for (int i = 0; i < 400; ++i) {
        if (ref.Range(0, 100) <= 7) {
            ++expected;
        }
        got += c.KillSomeOne(3, 6).healHp;
    }
    EXPECT_EQ(got, expected); // Random.Range(0,100) <= 7 (DISASM 0x266ee0 `cmp #7; bgt`)
    EXPECT_GT(got, 0);
    EXPECT_LT(got, 400);
    // At full health the roll is not even drawn.
    CharSkillC07 full(Hero().skillCd);
    full.SetRunBuffs(true, false);
    EXPECT_EQ(full.KillSomeOne(6, 6).healHp, 0);
}

TEST(CharSkillC07Test, KillSomeOneEnergyBuffRestoresTwoEnergy) {
    CharSkillC07 c(Hero().skillCd);
    c.SetRunBuffs(false, /*energy=*/true); // RGGameProcess.buff_energy (+0x48)
    EXPECT_EQ(c.KillSomeOne(6, 6).restoreEnergy, 2); // DISASM 0x266fec RestoreEnergy(2)
}

// --- the bat flock (DISASM BulletBat Start / FixedUpdate / FindTarget) ----------------------------

TEST(CharSkillC07Test, LaunchFansSixBatsWithTheRadiansQuirkAndMirroredFirstHalf) {
    BatSwarm s;
    s.Launch(6, {0.0F, 0.0F}, 5, BatSwarm::kSpeedUnitsPerSec);
    ASSERT_EQ(s.Alive(), 6U);
    // move_angle_k = -180 + 60k (k = 0..5), used as RADIANS by Start's cosf/sinf; the
    // first n = 3 bats are mirrored (localScale.x = -1: both components negated).
    for (int k = 0; k < 6; ++k) {
        const float a = static_cast<float>(-180 + 60 * k);
        const float sx = k < 3 ? -1.0F : 1.0F;
        const glm::vec2 v = s.VelOf(static_cast<std::size_t>(k));
        EXPECT_NEAR(v.x, 6.0F * sx * std::cos(a), 1e-4F) << "bat " << k;
        EXPECT_NEAR(v.y, 6.0F * sx * std::sin(a), 1e-4F) << "bat " << k;
        EXPECT_NEAR(std::sqrt(v.x * v.x + v.y * v.y), 6.0F, 1e-4F); // speed 6.0 units/s
    }
}

TEST(CharSkillC07Test, WithNoHostileEveryBatFlies0Point5SecondsThenReturnsAndMisses) {
    BatSwarm s;
    s.Launch(6, {0.0F, 0.0F}, 5, BatSwarm::kSpeedUnitsPerSec);
    std::vector<BatSwarm::Hit> hits;
    std::vector<bool> backs;
    const std::vector<SkillTarget> none;
    int firstBack = -1;
    for (int step = 1; step <= 100 && s.Alive() > 0; ++step) {
        if (step == 25) {
            // 24 free steps (the 25th has not run yet) at 6 u/s * 0.02 s = 2.88 units out,
            // still state 0.
            for (std::size_t i = 0; i < s.Alive(); ++i) {
                EXPECT_EQ(s.StateOf(i), 0);
                EXPECT_NEAR(std::sqrt(s.PosOf(i).x * s.PosOf(i).x + s.PosOf(i).y * s.PosOf(i).y),
                            2.88F, 1e-3F);
            }
        }
        s.Step(kFixedStepSeconds, {0.0F, 0.0F}, none, hits, backs);
        if (step == 25) { // FindTarget (0.5 s) found nobody -> state 2 (return)
            for (std::size_t i = 0; i < s.Alive(); ++i) {
                EXPECT_EQ(s.StateOf(i), 2);
            }
        }
        if (firstBack < 0 && !backs.empty()) {
            firstBack = step;
        }
    }
    // Return at 3x speed (0.36 units/step): after step 25 each bat is 2.52 out; the arrival
    // test (< 1.0) is evaluated at the start of a step: 2.52, 2.16, 1.80, 1.44, 1.08, 0.72
    // -> arrives on step 31.
    EXPECT_EQ(firstBack, 31);
    EXPECT_EQ(backs.size(), 6U);
    EXPECT_TRUE(hits.empty());
    for (const bool b : backs) {
        EXPECT_FALSE(b); // has_hit false: every bat is a miss
    }
    EXPECT_EQ(s.Alive(), 0U);
}

TEST(CharSkillC07Test, EveryBatHomesOnTheEnemyHitsItForFiveAndComesBackWithAHit) {
    BatSwarm s;
    s.Launch(6, {0.0F, 0.0F}, /*damage=*/5, BatSwarm::kSpeedUnitsPerSec);
    std::vector<BatSwarm::Hit> hits;
    std::vector<bool> backs;
    const std::vector<SkillTarget> hs = {SkillTarget{42, {5.0F, 0.0F}}};
    int steps = 0;
    while (s.Alive() > 0 && steps < 1500) {
        s.Step(kFixedStepSeconds, {0.0F, 0.0F}, hs, hits, backs);
        ++steps;
    }
    ASSERT_EQ(s.Alive(), 0U) << "the steering never converged";
    ASSERT_EQ(hits.size(), 6U); // each bat hits exactly once (has_hit latch)
    int total = 0;
    for (const BatSwarm::Hit &h : hits) {
        EXPECT_EQ(h.id, 42U);
        EXPECT_EQ(h.damage, 5);
        total += h.damage;
    }
    EXPECT_EQ(total, 30);
    ASSERT_EQ(backs.size(), 6U);
    for (const bool b : backs) {
        EXPECT_TRUE(b); // every bat returns with has_hit = true
    }
}

TEST(CharSkillC07Test, AHostileBeyondTwelveUnitsIsNotAFindTargetCandidate) {
    BatSwarm s;
    s.Launch(2, {0.0F, 0.0F}, 5, BatSwarm::kSpeedUnitsPerSec);
    std::vector<BatSwarm::Hit> hits;
    std::vector<bool> backs;
    std::vector<SkillTarget> hs = {SkillTarget{7, {20.0F, 0.0F}}}; // 12-unit radius never reaches it
    for (int i = 0; i < 25; ++i) {
        s.Step(kFixedStepSeconds, {0.0F, 0.0F}, hs, hits, backs);
    }
    // 20 units away is out of every bat's 12-unit FindTarget circle (bats are within ~2.9
    // units of the origin): they all took the "no target" branch.
    for (std::size_t i = 0; i < s.Alive(); ++i) {
        EXPECT_EQ(s.StateOf(i), 2);
    }
}

// NOLINTEND(readability-magic-numbers)
