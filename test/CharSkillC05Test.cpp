#include <gtest/gtest.h>

#include <vector>

#include "combat/CharSkillC05.hpp"
#include "data/GameData.hpp"
#include "sim/SimConfig.hpp"

using Game::CharSkillC05;
using Game::Sim::kFixedStepMs;
using Game::Sim::kFixedStepSeconds;

// P3.3.c C05 (Alchemist) brain. Expected numbers: characters.json (skill_cd 6, bottle_objs
// has 4 serialized refs, through GameData) and the decompile constants cited at their
// declarations in CharSkillC05.hpp (DISASM 0x265df4-0x2661e8, typed 1549-1638).

// NOLINTBEGIN(readability-magic-numbers)

namespace {
const Game::CharacterDef &Hero() {
    static Game::GameData gd;
    static const bool ok = gd.LoadAll(RESOURCE_DIR);
    EXPECT_TRUE(ok);
    const Game::CharacterDef *c = gd.FindCharacter("c05");
    EXPECT_NE(c, nullptr);
    static const Game::CharacterDef fallback{};
    return c != nullptr ? *c : fallback;
}

void TickSteps(CharSkillC05 &c, int n) {
    for (int i = 0; i < n; ++i) {
        c.Tick(kFixedStepMs);
    }
}
} // namespace

TEST(CharSkillC05Test, DataRowIsTheAlchemist) {
    EXPECT_FLOAT_EQ(Hero().skillCd, 6.0F); // characters.json c05 skill_cd
    EXPECT_EQ(Hero().atk, 4);
    EXPECT_EQ(Hero().SkillRefCount("bottle_objs"), 4U); // c05.prefab bottle_objs[0..3]
}

// --- activation gate (TYPED 1549-1577) -----------------------------------------------

TEST(CharSkillC05Test, GateIsSkillReadyAndNotInSkill) {
    CharSkillC05 c(Hero().skillCd);
    EXPECT_TRUE(c.SkillReady());
    const CharSkillC05::RoleSkillResult r = c.RoleSkill(0, 4, false);
    EXPECT_TRUE(r.activated);
    // Still on cooldown (RoleSkillEnd restarted it): a second press does nothing.
    const CharSkillC05::RoleSkillResult again = c.RoleSkill(0, 4, false);
    EXPECT_FALSE(again.activated);
    EXPECT_EQ(again.bottleCount, 0);
}

TEST(CharSkillC05Test, TheUltimateIsInstantInSkillNeverObservableAfterTheCall) {
    // DISASM 0x2661e8: RoleSkillEnd is vcall'd at the tail of the SAME RoleSkill body.
    CharSkillC05 c(Hero().skillCd);
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());
}

// --- effect magnitude: bottles per cast, fan and variant -------------------------------

TEST(CharSkillC05Test, OneBottleNormallyThreeInAFanWhenStrengthened) {
    CharSkillC05 a(Hero().skillCd);
    const CharSkillC05::RoleSkillResult n = a.RoleSkill(0, 4, /*skillStrengthen=*/false);
    EXPECT_EQ(n.bottleCount, 1); // DISASM 0x265df4-0x265e00
    EXPECT_EQ(n.fanOffsetsDeg, (std::vector<float>{0.0F}));
    CharSkillC05 b(Hero().skillCd);
    const CharSkillC05::RoleSkillResult s = b.RoleSkill(0, 4, /*skillStrengthen=*/true);
    EXPECT_EQ(s.bottleCount, 3);
    EXPECT_EQ(s.fanOffsetsDeg, (std::vector<float>{-20.0F, 0.0F, 20.0F})); // -20 then += 0x14
    EXPECT_EQ(CharSkillC05::kBottlesNormal, 1);
    EXPECT_EQ(CharSkillC05::kBottlesStrengthened, 3);
}

TEST(CharSkillC05Test, TheBottleVariantFollowsTheCurrentSkinElseElementZero) {
    const int variants = static_cast<int>(Hero().SkillRefCount("bottle_objs")); // 4
    CharSkillC05 a(Hero().skillCd);
    EXPECT_EQ(a.RoleSkill(2, variants, false).variantIndex, 2); // bottle_objs[skin 2]
    CharSkillC05 b(Hero().skillCd);
    EXPECT_EQ(b.RoleSkill(7, variants, false).variantIndex, 0); // skin beyond the array
    CharSkillC05 c(Hero().skillCd);
    const CharSkillC05::RoleSkillResult r = c.RoleSkill(0, variants, false);
    EXPECT_EQ(r.variantIndex, 0);
    EXPECT_EQ(r.variantCount, 4);
}

// --- end / reset + cooldown at the fixed step (TYPED 1517-1526 Update, 1584-1638) ------

TEST(CharSkillC05Test, CooldownRestartsAtTheCastAndCountsUpEveryFixedStep) {
    CharSkillC05 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 6.0F); // ReSetSkillReload: this_skill_time = 0
    TickSteps(c, 150); // 150 * 0.02 s = 3.0 s
    EXPECT_NEAR(c.CooldownRemaining(), 6.0F - 150 * kFixedStepSeconds, 1e-3F); // 3.0
    EXPECT_FALSE(c.SkillReady());
    TickSteps(c, 160); // past 6 s
    EXPECT_TRUE(c.SkillReady());
    EXPECT_TRUE(c.TryActivateSkill());
}

TEST(CharSkillC05Test, EndSkillIsUnconditionalAndRestartsTheCooldown) {
    // TYPED 1584-1638 has no guard: in_skill = false; ReSetSkillReload.
    CharSkillC05 c(Hero().skillCd);
    EXPECT_TRUE(c.SkillReady());
    c.EndSkill();
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 6.0F);
}

TEST(CharSkillC05Test, NegativeCooldownClampsAndNoRngIsDrawn) {
    CharSkillC05 neg(-1.0F);
    EXPECT_FLOAT_EQ(neg.SkillCd(), 0.0F);
    CharSkillC05 a(Hero().skillCd);
    CharSkillC05 b(Hero().skillCd);
    a.SetSeed(5);
    b.SetSeed(5);
    for (int i = 0; i < 4; ++i) {
        a.TryActivateSkill();
        TickSteps(a, 301);
    }
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
