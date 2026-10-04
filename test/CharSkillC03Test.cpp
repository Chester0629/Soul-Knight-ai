#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "combat/CharSkillC03.hpp"
#include "data/GameData.hpp"
#include "sim/SimConfig.hpp"

using Game::CharSkillC03;
using Game::SkillTarget;
using Game::ThunderChain;
using Game::Sim::kFixedStepMs;
using Game::Sim::kFixedStepSeconds;

// P3.3.c C03 (Wizard) brain. Every expected number is either read from
// Resources/data/characters.json (skill_cd 6 for c03, through GameData) or is a decompile
// constant cited at its declaration in CharSkillC03.hpp (typed game_typed.c lines, or
// the ARM disassembly addresses for T-39).

// NOLINTBEGIN(readability-magic-numbers)

namespace {
const Game::CharacterDef &Hero() {
    static Game::GameData gd;
    static const bool ok = gd.LoadAll(RESOURCE_DIR);
    EXPECT_TRUE(ok);
    const Game::CharacterDef *c = gd.FindCharacter("c03");
    EXPECT_NE(c, nullptr);
    static const Game::CharacterDef fallback{};
    return c != nullptr ? *c : fallback;
}

// Run @p n fixed steps of the brain's Tick.
void TickSteps(CharSkillC03 &c, int n) {
    for (int i = 0; i < n; ++i) {
        c.Tick(kFixedStepMs);
    }
}

SkillTarget T(std::uint32_t id, float x, float y) { return SkillTarget{id, {x, y}}; }
} // namespace

// --- activation gate (TYPED 872-901: skill_ready && !in_skill) ------------------------

TEST(CharSkillC03Test, DataRowIsTheWizard) {
    EXPECT_FLOAT_EQ(Hero().skillCd, 6.0F); // characters.json c03 skill_cd
    EXPECT_EQ(Hero().atk, 3);
    EXPECT_EQ(Hero().SkillRefCount("thunder"), 1U); // c03.prefab `thunder` (serialized ref)
}

TEST(CharSkillC03Test, GateNeedsReadyAndNotInSkill) {
    CharSkillC03 c(Hero().skillCd);
    EXPECT_TRUE(c.SkillReady()); // starts ready
    EXPECT_FALSE(c.InSkill());
    EXPECT_TRUE(c.TryActivateSkill());
    EXPECT_TRUE(c.InSkill()); // in_skill = 1 (DISASM 0x26446c)
    EXPECT_FALSE(c.TryActivateSkill()); // !in_skill gate blocks a re-cast while active
    // Spend the charge: the gate now fails on skill_ready.
    c.CreateTunder(false);
    EXPECT_FALSE(c.InSkill());
    EXPECT_FALSE(c.SkillReady());
    EXPECT_FALSE(c.TryActivateSkill());
}

// --- the 0.4 s Invoke (DISASM 0x26449c: Invoke("CreateTunder", 0.4f)) -----------------

TEST(CharSkillC03Test, CreateTunderIsDueExactlyFourTenthsOfASecondAfterThePress) {
    CharSkillC03 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    EXPECT_FALSE(c.CreateTunderDue());
    TickSteps(c, 19); // 19 * 20 ms = 380 ms
    EXPECT_FALSE(c.CreateTunderDue());
    TickSteps(c, 1); // 400 ms
    EXPECT_TRUE(c.CreateTunderDue());
    EXPECT_TRUE(c.InSkill()); // still in_skill until CreateTunder -> RoleSkillEnd runs
}

// --- effect magnitude (DISASM 0x264ba0: StartThunder(.., 8)) ---------------------------

TEST(CharSkillC03Test, CreateTunderStrikesForEightWhenATargetWasFound) {
    CharSkillC03 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    TickSteps(c, 20);
    ASSERT_TRUE(c.CreateTunderDue());
    const CharSkillC03::TunderDecision d = c.CreateTunder(/*targetFound=*/true);
    EXPECT_TRUE(d.strike);
    EXPECT_EQ(d.damage, 8);
    EXPECT_EQ(CharSkillC03::kStrikeDamage, 8);
    EXPECT_FALSE(c.CreateTunderDue()); // the Invoke was consumed
}

TEST(CharSkillC03Test, NoTargetStillSpendsTheCooldownButSpawnsNothing) {
    CharSkillC03 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    TickSteps(c, 20);
    const CharSkillC03::TunderDecision d = c.CreateTunder(/*targetFound=*/false);
    EXPECT_FALSE(d.strike);
    EXPECT_EQ(d.damage, 0);
    // RoleSkillEnd ran anyway (vcall slot 25 @0x264bf4): the charge is spent.
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 6.0F);
}

// --- end / reset + cooldown at the fixed step (TYPED 841-850 Update, 924-936) ----------

TEST(CharSkillC03Test, EndRestartsTheCooldownAndItCountsUpEveryFixedStep) {
    CharSkillC03 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    TickSteps(c, 20);
    c.CreateTunder(true); // -> RoleSkillEnd: this_skill_time = 0
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 6.0F);
    TickSteps(c, 100); // 100 * 0.02 s = 2.0 s
    EXPECT_NEAR(c.CooldownRemaining(), 6.0F - 100 * kFixedStepSeconds, 1e-3F); // 4.0
    EXPECT_FALSE(c.SkillReady());
    TickSteps(c, 210); // +4.2 s -> past the 6 s cooldown, clamped
    EXPECT_TRUE(c.SkillReady());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 0.0F);
    EXPECT_TRUE(c.TryActivateSkill()); // cast again
}

TEST(CharSkillC03Test, UpdateNeverEndsTheSkillOnATimer) {
    // TYPED 841-850: C03's Update has no in_skill_time countdown; only the armed
    // CreateTunder Invoke can end it. Without CreateTunder running the skill stays active.
    CharSkillC03 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    TickSteps(c, 2000); // 40 s
    EXPECT_TRUE(c.InSkill());
}

TEST(CharSkillC03Test, DeadChildClearsInSkillAndRestartsTheCooldown) {
    // TYPED 943-956: in_skill = false; this_skill_time = 0.0 (unconditional).
    CharSkillC03 c(Hero().skillCd);
    ASSERT_TRUE(c.TryActivateSkill());
    c.DeadChild();
    EXPECT_FALSE(c.InSkill());
    EXPECT_FLOAT_EQ(c.CooldownRemaining(), 6.0F);
    EXPECT_FALSE(c.CreateTunderDue());
    CharSkillC03 idle(Hero().skillCd);
    idle.DeadChild(); // also resets a READY idle hero (no guard in the body)
    EXPECT_FALSE(idle.SkillReady());
}

TEST(CharSkillC03Test, NegativeCooldownClampsAndNoRngIsDrawn) {
    CharSkillC03 neg(-3.0F);
    EXPECT_FLOAT_EQ(neg.SkillCd(), 0.0F);
    CharSkillC03 a(Hero().skillCd);
    CharSkillC03 b(Hero().skillCd);
    a.SetSeed(99);
    b.SetSeed(99);
    for (int i = 0; i < 4; ++i) {
        a.TryActivateSkill();
        TickSteps(a, 20);
        a.CreateTunder(true);
        TickSteps(a, 300);
    }
    EXPECT_EQ(a.Rng().Range(0, 1000000), b.Rng().Range(0, 1000000));
}

// --- CreateTunder's target scan (DISASM 0x2646ac-0x2649f8) ------------------------------

TEST(CharSkillC03Test, ScanPicksTheNearestHostileWithinFourteenUnits) {
    const std::vector<SkillTarget> hs = {T(7, 9.0F, 0.0F), T(8, 3.0F, 4.0F), T(9, 13.9F, 0.0F),
                                         T(10, 14.5F, 0.0F)};
    // distances from the origin: 9, 5, 13.9, 14.5 -> index 1 (id 8) wins.
    EXPECT_EQ(CharSkillC03::PickNearestTarget(hs, {0.0F, 0.0F}, {}), 1);
    // Only the far one is out of the 14-unit circle: dropping the near ones picks 13.9.
    const std::vector<SkillTarget> far = {T(9, 13.9F, 0.0F), T(10, 14.5F, 0.0F)};
    EXPECT_EQ(CharSkillC03::PickNearestTarget(far, {0.0F, 0.0F}, {}), 0);
    const std::vector<SkillTarget> out = {T(10, 14.5F, 0.0F)};
    EXPECT_EQ(CharSkillC03::PickNearestTarget(out, {0.0F, 0.0F}, {}), -1);
    EXPECT_EQ(CharSkillC03::PickNearestTarget({}, {0.0F, 0.0F}, {}), -1);
}

TEST(CharSkillC03Test, ScanSkipsAWallBlockedHostileAndUsesTheHalfUnitLosOffset) {
    const std::vector<SkillTarget> hs = {T(1, 2.0F, 0.0F), T(2, 6.0F, 0.0F)};
    glm::vec2 seenA{0.0F, 0.0F};
    glm::vec2 seenB{0.0F, 0.0F};
    // Block only the nearest one (id 1 at x = 2); the LOS endpoints must be lifted by 0.5 in y.
    const Game::SkillLineBlockedFn blocked = [&](glm::vec2 a, glm::vec2 b) {
        if (a.x < 3.0F) {
            seenA = a;
            seenB = b;
            return true;
        }
        return false;
    };
    EXPECT_EQ(CharSkillC03::PickNearestTarget(hs, {0.0F, 0.0F}, blocked), 1); // id 2 instead
    EXPECT_FLOAT_EQ(seenA.y, 0.5F); // hit.y + 0.5 (pool 0x264a04)
    EXPECT_FLOAT_EQ(seenB.y, 0.5F); // player.y + 0.5
    EXPECT_FLOAT_EQ(seenB.x, 0.0F);
}

TEST(CharSkillC03Test, ScanFirstOfEqualDistancesWins) {
    const std::vector<SkillTarget> hs = {T(1, 5.0F, 0.0F), T(2, 0.0F, 5.0F), T(3, -5.0F, 0.0F)};
    EXPECT_EQ(CharSkillC03::PickNearestTarget(hs, {0.0F, 0.0F}, {}), 0); // strict `<`
}

// --- the thunder chain (DISASM 0xafef08-0xaffdd0) ---------------------------------------

namespace {
// Step the chain until it produces hits or finishes; returns the step count taken (1-based)
// and fills @p hits. Gives up after @p maxSteps.
int StepsUntilHit(ThunderChain &ch, const std::vector<SkillTarget> &hs,
                  std::vector<ThunderChain::Hit> &hits, int maxSteps = 60) {
    for (int i = 1; i <= maxSteps; ++i) {
        ch.Step(kFixedStepSeconds, hs, hits);
        if (!hits.empty() || ch.Done()) {
            return i;
        }
    }
    return -1;
}
} // namespace

TEST(CharSkillC03Test, ChainHopsToTheNearestUnhitTargetEveryEightSteps) {
    // Strike point A at the origin (id 0, already hit by StartThunder). Candidates within
    // 10 units of A: B(4), C(8), D(9), F(9.5), G(9.8); E(12) is out of range.
    const std::vector<SkillTarget> hs = {T(0, 0, 0), T(1, 4, 0), T(2, 8, 0),   T(3, 0, 9),
                                         T(4, 12, 0), T(5, 0, -9.5F), T(6, 0, 9.8F)};
    ThunderChain ch;
    ch.Start(hs[0], /*atk=*/8, /*intensify=*/false);
    std::vector<std::uint32_t> order;
    std::vector<int> stepsAt;
    int total = 0;
    while (!ch.Done() && total < 400) {
        std::vector<ThunderChain::Hit> hits;
        ch.Step(kFixedStepSeconds, hs, hits);
        ++total;
        for (const ThunderChain::Hit &h : hits) {
            order.push_back(h.id);
            stepsAt.push_back(total);
            EXPECT_EQ(h.damage, 8); // not intensified: damage = atk
        }
    }
    // Four hops, then the count gate (old count 4 > 3) ends the chain although G (9.8)
    // is still in range: kCountGate + 1 hops.
    ASSERT_EQ(order.size(), 4U);
    EXPECT_EQ(order, (std::vector<std::uint32_t>{1, 2, 3, 5}));
    EXPECT_EQ(ch.HopCount(), 4);
    EXPECT_EQ(ThunderChain::kCountGate + 1, 4);
    EXPECT_TRUE(ch.Done());
    // Invoke(0.15) re-arms from the firing frame: 8 fixed steps (0.16 s) per hop.
    ASSERT_EQ(stepsAt.size(), 4U);
    EXPECT_EQ(stepsAt[0], 8);
    EXPECT_EQ(stepsAt[1] - stepsAt[0], 8);
    EXPECT_EQ(stepsAt[2] - stepsAt[1], 8);
    EXPECT_EQ(stepsAt[3] - stepsAt[2], 8);
}

TEST(CharSkillC03Test, IntensifiedHopsAddTwoPerHop) {
    const std::vector<SkillTarget> hs = {T(0, 0, 0), T(1, 2, 0), T(2, 4, 0), T(3, 6, 0)};
    ThunderChain ch;
    ch.Start(hs[0], 8, /*intensify=*/true);
    std::vector<int> damages;
    for (int i = 0; i < 100 && !ch.Done(); ++i) {
        std::vector<ThunderChain::Hit> hits;
        ch.Step(kFixedStepSeconds, hs, hits);
        for (const ThunderChain::Hit &h : hits) {
            damages.push_back(h.damage);
        }
    }
    // atk + 2 * count, with count incremented BEFORE the hit (DISASM 0xaff604 / 0xaffab8).
    EXPECT_EQ(damages, (std::vector<int>{10, 12, 14}));
}

TEST(CharSkillC03Test, ALoneTargetIsReStruckForDoubleDamage) {
    const std::vector<SkillTarget> hs = {T(0, 0, 0), T(1, 11, 0)}; // 11 > 10: not a candidate
    ThunderChain ch;
    ch.Start(hs[0], 8, false);
    std::vector<ThunderChain::Hit> hits;
    const int at = StepsUntilHit(ch, hs, hits);
    EXPECT_EQ(at, 8);
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits[0].id, 0U);
    EXPECT_EQ(hits[0].damage, 16); // atk * 2 (DISASM 0xaff438 `lsl r1, r4, #1`)
    EXPECT_TRUE(ch.Done());
}

TEST(CharSkillC03Test, TheReStrikeNeedsTheTargetToStillExist) {
    const std::vector<SkillTarget> gone = {T(1, 11, 0)}; // the struck target (id 0) died
    ThunderChain ch;
    ch.Start(T(0, 0, 0), 8, false);
    std::vector<ThunderChain::Hit> hits;
    EXPECT_EQ(StepsUntilHit(ch, gone, hits), 8);
    EXPECT_TRUE(hits.empty());
    EXPECT_TRUE(ch.Done());
}

TEST(CharSkillC03Test, AChainThatAlreadyHoppedEndsWithoutAReStrike) {
    std::vector<SkillTarget> hs = {T(0, 0, 0), T(1, 3, 0)};
    ThunderChain ch;
    ch.Start(hs[0], 8, false);
    std::vector<ThunderChain::Hit> hits;
    ASSERT_EQ(StepsUntilHit(ch, hs, hits), 8); // hop to id 1
    ASSERT_EQ(hits.size(), 1U);
    hits.clear();
    EXPECT_EQ(StepsUntilHit(ch, hs, hits), 8); // nothing left, count != 0 -> just ends
    EXPECT_TRUE(hits.empty());
    EXPECT_TRUE(ch.Done());
}

// NOLINTEND(readability-magic-numbers)
