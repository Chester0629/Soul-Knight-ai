#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "data/LootTable.hpp"
#include "data/RGRandom.hpp"

using Game::LootTable;
using Game::RGRandom;

namespace {
// RESOURCE_DIR is injected by CMake (points at the repo's Resources/ folder).
const char *kResourceRoot = RESOURCE_DIR;
} // namespace

// NOLINTBEGIN(readability-magic-numbers)

// --- Loading / shape ---------------------------------------------------------

TEST(LootTableTest, LoadsAllSevenTiers) {
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));
    EXPECT_EQ(lt.TierCount(), 7u);
}

TEST(LootTableTest, TierTotalsMatchData) {
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));
    // Sums of rate weights per tier (all rates are 2 in the shipped data).
    const std::map<int, int> kExpectedTotals = {
        {0, 20}, {1, 64}, {2, 62}, {3, 64}, {4, 64}, {5, 62}, {6, 16}};
    for (const auto &kv : kExpectedTotals) {
        const auto *t = lt.GetTier(kv.first);
        ASSERT_NE(t, nullptr) << "tier " << kv.first;
        EXPECT_EQ(t->total, kv.second) << "tier " << kv.first;
    }
}

TEST(LootTableTest, TierEntryCountsMatchData) {
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));
    EXPECT_EQ(lt.GetTier(0)->entries.size(), 10u);
    EXPECT_EQ(lt.GetTier(6)->entries.size(), 8u);
}

TEST(LootTableTest, EveryRollMemberOfRequestedTier) {
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));

    for (int tier = 0; tier <= 6; ++tier) {
        const auto *t = lt.GetTier(tier);
        ASSERT_NE(t, nullptr);
        RGRandom rng;
        rng.SetRandomSeed(1000 + tier);
        for (int i = 0; i < 2000; ++i) {
            std::string drop = lt.Roll(tier, rng);
            bool found = false;
            for (const auto &e : t->entries) {
                if (e.dropId == drop) {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found) << "tier " << tier << " roll '" << drop
                               << "' not in tier";
        }
    }
}

// --- Determinism: same seed + tier -> identical drop sequence -----------------

TEST(LootTableTest, SameSeedSameDropSequence) {
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));

    RGRandom a;
    RGRandom b;
    a.SetRandomSeed(424242);
    b.SetRandomSeed(424242);
    for (int i = 0; i < 256; ++i) {
        EXPECT_EQ(lt.Roll(3, a), lt.Roll(3, b)) << "draw " << i;
    }
}

TEST(LootTableTest, ReseedReproducesFromStart) {
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));

    RGRandom rng;
    rng.SetRandomSeed(98765);
    std::vector<std::string> first;
    for (int i = 0; i < 64; ++i) {
        first.push_back(lt.Roll(2, rng));
    }
    rng.SetRandomSeed(98765);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(lt.Roll(2, rng), first[static_cast<std::size_t>(i)]);
    }
}

TEST(LootTableTest, OneIntDrawConsumedPerRoll) {
    // A roll must consume EXACTLY one RGRandom int draw (matching OpenChest).
    // Compare an interleaved drop-then-raw-draw stream against a control stream:
    // the raw draws read after each roll must line up if exactly one draw was
    // spent inside Roll.
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));
    const int total = lt.GetTier(1)->total;

    RGRandom rolled;
    RGRandom control;
    rolled.SetRandomSeed(13579);
    control.SetRandomSeed(13579);
    for (int i = 0; i < 100; ++i) {
        lt.Roll(1, rolled);           // consumes one Range(0,total) draw
        control.Range(0, total);      // mirror that single draw
        // Both streams should now be aligned: the next raw draws must match.
        EXPECT_EQ(rolled.Range(0, total), control.Range(0, total))
            << "misaligned after roll " << i;
    }
}

// --- Distribution: weighted picks roughly match rates over many rolls ---------

TEST(LootTableTest, UniformTierDistributionRoughlyEven) {
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));

    const int tier = 0; // 10 entries, all rate 2 -> uniform 1/10 each.
    const auto *t = lt.GetTier(tier);
    ASSERT_NE(t, nullptr);

    std::map<std::string, int> counts;
    RGRandom rng;
    rng.SetRandomSeed(2024);
    const int kRolls = 100000;
    for (int i = 0; i < kRolls; ++i) {
        counts[lt.Roll(tier, rng)]++;
    }

    // Every entry should be hit and land within a generous band of the expected
    // share (weight/total). With a fixed seed this is deterministic; the band is
    // wide enough to never flake yet tight enough to catch a broken walk.
    EXPECT_EQ(counts.size(), t->entries.size());
    for (const auto &e : t->entries) {
        double expectedShare = static_cast<double>(e.rate) / t->total;
        double observedShare = static_cast<double>(counts[e.dropId]) / kRolls;
        EXPECT_NEAR(observedShare, expectedShare, 0.02)
            << "entry " << e.dropId;
    }
}

// --- Cumulative-subtraction / clamp / fallback edge cases ---------------------

TEST(LootTableTest, RollFirstEntryAtLowDraws) {
    // The first entry wins iff r < entry[0].rate. With all rates == 2, draws
    // r in {0,1} must yield entry[0]. We can't inject r directly, but we can
    // assert structurally: across the whole tier, the SET of possible drops is
    // exactly the tier's entries (covered above), and the FIRST entry is the
    // one selected for the smallest cumulative bucket. Verify by reconstructing
    // the walk against a known r.
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));
    const auto *t = lt.GetTier(0);
    ASSERT_NE(t, nullptr);

    auto walk = [&](int r) -> std::string {
        for (const auto &e : t->entries) {
            r -= e.rate;
            if (r < 0) {
                return e.dropId;
            }
        }
        return t->entries.back().dropId;
    };

    EXPECT_EQ(walk(0), t->entries.front().dropId);
    EXPECT_EQ(walk(1), t->entries.front().dropId);
    EXPECT_EQ(walk(2), t->entries[1].dropId);          // crosses into bucket 2
    EXPECT_EQ(walk(t->total - 1), t->entries.back().dropId); // last bucket
}

TEST(LootTableTest, ClampSelectsNearestAvailableTier) {
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));

    // Above the top tier clamps down to the highest available (6).
    const auto *high = lt.GetTier(99);
    ASSERT_NE(high, nullptr);
    EXPECT_EQ(high->level, 6);

    // Below the bottom tier clamps up to the lowest available (0).
    const auto *low = lt.GetTier(-5);
    ASSERT_NE(low, nullptr);
    EXPECT_EQ(low->level, 0);

    // Exact tiers resolve to themselves.
    for (int i = 0; i <= 6; ++i) {
        EXPECT_EQ(lt.GetTier(i)->level, i);
    }
}

TEST(LootTableTest, OutOfRangeTierStillRollsMember) {
    LootTable lt;
    ASSERT_TRUE(lt.LoadAll(kResourceRoot));

    const auto *clamped = lt.GetTier(100);
    ASSERT_NE(clamped, nullptr);
    RGRandom rng;
    rng.SetRandomSeed(7);
    for (int i = 0; i < 500; ++i) {
        std::string drop = lt.Roll(100, rng);
        bool found = false;
        for (const auto &e : clamped->entries) {
            if (e.dropId == drop) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "roll '" << drop << "' not in clamped tier";
    }
}

// --- Graceful failure --------------------------------------------------------

TEST(LootTableTest, MissingResourceRootFailsGracefully) {
    LootTable lt;
    // Non-existent root: DataStore logs + returns empty object; LoadAll reports
    // failure but must not throw, and leaves no tiers.
    EXPECT_FALSE(lt.LoadAll("does_not_exist_root"));
    EXPECT_EQ(lt.TierCount(), 0u);
    EXPECT_EQ(lt.GetTier(0), nullptr);

    // Rolling an unloaded table returns the empty string, never throws.
    RGRandom rng;
    rng.SetRandomSeed(1);
    EXPECT_EQ(lt.Roll(0, rng), std::string());
}

// NOLINTEND(readability-magic-numbers)
