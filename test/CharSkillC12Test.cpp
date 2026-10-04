#include <gtest/gtest.h>

#include "combat/CharSkillC12.hpp"
#include "data/GameData.hpp"
#include "sim/SimConfig.hpp"

using Game::CharSkillC12;
using Game::Sim::kFixedStepMs;
using Game::Sim::kFixedStepSeconds;

// P3.3.c C12 (Druid) brain. Expected numbers: characters.json (skill_cd 2.5, atk 5,
// critical 5, through GameData) and the decompile constants cited in CharSkillC12.hpp
// (typed 3674-3749; DISASM 0x26b26c-0x26b7d4 for the two phases).

// NOLINTBEGIN(readability-magic-numbers)

namespace {
const Game::CharacterDef &Hero() {
    static Game::GameData gd;
    static const bool ok = gd.LoadAll(RESOURCE_DIR);
    EXPECT_TRUE(ok);
    const Game::CharacterDef *c = gd.FindCharacter("c12");
    EXPECT_NE(c, nullptr);
    static const Game::CharacterDef fallback{};
    return c != nullptr ? *c : fallback;
}

void TickSteps(CharSkillC12 &c, int n) {
    for (int i = 0; i < n; ++i) {
        c.Tick(kFixedStepMs);
    }
}
} // namespace

TEST(CharSkillC12Test, DataRowIsTheDruid) {
    EXPECT_FLOAT_EQ(Hero().skillCd, 2.5F); // characters.json c12 skill_cd
    EXPECT_EQ(Hero().atk, 5);
    EXPECT_EQ(Hero().critical, 5);
    EXPECT_EQ(Hero().skillRefs.at("skill_obj1").front(), "wolf1"); // c12.prefab
    EXPECT_EQ(Hero().skillRefs.at("skill_obj2").front(), "wolf2");
}

// --- activation gate (TYPED 3706-3729: skill_ready ONLY, no in_skill test) ----------------

TEST(CharSkillC12Test, TheFirstCastSummonsTwoWolvesAndLeavesInSkillSet) {
    CharSkillC12 c(Hero().skillCd);
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FALSE(c.InSkill());
    const CharSkillC12::RoleSkillResult r = c.RoleSkill(Hero().atk, Hero().critical, false);
    EXPECT_EQ(r.phase, CharSkillC12::Phase::Summon);
    EXPECT_EQ(r.wolves, 2); // skill_obj1 + skill_obj2 (DISASM 0x26b45c / 0x26b5dc)
    EXPECT_FALSE(r.strengthened);
    EXPECT_TRUE(c.InSkill()); // in_skill = 1 @0x26b308 ...
    EXPECT_FALSE(c.SkillReady()); // ... and RoleSkillEnd restarted the cooldown
}

TEST(CharSkillC12Test, TheGateIgnoresInSkillAndOnlyWaitsForTheCooldown) {
    CharSkillC12 c(Hero().skillCd);
    ASSERT_EQ(c.RoleSkill(5, 5, false).phase, CharSkillC12::Phase::Summon);
    // in_skill is set, yet while the cooldown runs the gate still refuses (skill_ready).
    EXPECT_EQ(c.RoleSkill(5, 5, false).phase, CharSkillC12::Phase::None);
    TickSteps(c, 100); // 2.0 s: not yet
    EXPECT_EQ(c.RoleSkill(5, 5, false).phase, CharSkillC12::Phase::None);
    TickSteps(c, 30); // 3.0 s: ready (the float count-up is exact only to ~1e-5)
    EXPECT_TRUE(c.SkillReady());
    EXPECT_NE(c.RoleSkill(5, 5, false).phase, CharSkillC12::Phase::None);
}

// --- effect magnitude: the second phase is RGHand.AtkCut(atk, critical) ---------------------

TEST(CharSkillC12Test, EveryLaterCastIsAnAtkCutWithTheHeroAtkAndCritical) {
    CharSkillC12 c(Hero().skillCd);
    ASSERT_EQ(c.RoleSkill(Hero().atk, Hero().critical, false).phase, CharSkillC12::Phase::Summon);
    TickSteps(c, 130);
    const CharSkillC12::RoleSkillResult cut = c.RoleSkill(Hero().atk, Hero().critical, false);
    EXPECT_EQ(cut.phase, CharSkillC12::Phase::Cut);
    EXPECT_EQ(cut.cutDamage, 5); // role_attribute.atk (+0x40), DISASM 0x26b2b4-0x26b2e0
    EXPECT_EQ(cut.cutCritical, 5); // role_attribute.critical (+0x24)
    EXPECT_EQ(cut.wolves, 0);
    // in_skill is NEVER cleared (RoleSkillEnd 3736-3749 has no in_skill store): the next
    // cast after the cooldown is again a cut, not another summon.
    EXPECT_TRUE(c.InSkill());
    TickSteps(c, 130);
    EXPECT_EQ(c.RoleSkill(Hero().atk, Hero().critical, false).phase, CharSkillC12::Phase::Cut);
}

TEST(CharSkillC12Test, TheStrengthenedFlagRidesTheSummonOnly) {
    CharSkillC12 c(Hero().skillCd);
    const CharSkillC12::RoleSkillResult r = c.RoleSkill(5, 5, /*skillStrengthen=*/true);
    EXPECT_TRUE(r.strengthened); // WolfController.strengthen (+0x80) = true
}

// --- end / reset + cooldown at the fixed step (TYPED 3674-3683, 3736-3749) --------------------

TEST(CharSkillC12Test, RoleSkillEndRestartsTheCooldownAndItCountsUpEveryFixedStep) {
    CharSkillC12 c(Hero().skillCd);
    ASSERT_NE(c.RoleSkill(5, 5, false).phase, CharSkillC12::Phase::None);
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 2.5F);
    TickSteps(c, 62); // 1.24 s
    EXPECT_NEAR(c.CooldownRemaining(), 2.5F - 62 * kFixedStepSeconds, 1e-3F); // 1.26
}

TEST(CharSkillC12Test, EndSkillOnlyResetsTheCooldownAndNeverClearsInSkill) {
    CharSkillC12 c(Hero().skillCd);
    ASSERT_EQ(c.RoleSkill(5, 5, false).phase, CharSkillC12::Phase::Summon);
    TickSteps(c, 130);
    ASSERT_TRUE(c.SkillReady());
    c.EndSkill();
    EXPECT_FALSE(c.SkillReady());
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC12Test, NegativeCooldownClampsAndNoRngIsDrawn) {
    CharSkillC12 neg(-1.0F);
    EXPECT_FLOAT_EQ(neg.SkillCd(), 0.0F);
    CharSkillC12 a(Hero().skillCd);
    CharSkillC12 b(Hero().skillCd);
    a.SetSeed(12);
    b.SetSeed(12);
    for (int i = 0; i < 4; ++i) {
        a.RoleSkill(5, 5, false);
        TickSteps(a, 130);
    }
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
