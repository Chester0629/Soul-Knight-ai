#include <gtest/gtest.h>

#include "combat/RGBTDivision.hpp"

using Game::RGBTDivision;

// NOLINTBEGIN(readability-magic-numbers)

// --- OnTriggerEnter2D: the once-only hit-flag arm ----------------------------

TEST(RGBTDivisionTest, StartsNotDestroyed) {
    // Fresh bullet: `destroyed` (0x64) is false before any hit.
    RGBTDivision d;
    EXPECT_FALSE(d.Destroyed());
}

TEST(RGBTDivisionTest, MatchingHitArmsDestroyedFlag) {
    // A recognised-tag collision sets *(this+0x64) = 1.
    RGBTDivision d;
    d.OnTriggerEnter2D(true);
    EXPECT_TRUE(d.Destroyed());
}

TEST(RGBTDivisionTest, NonMatchingHitLeavesFlagUnset) {
    // No recognised tag matched -> the decomp's `return` before the flag set:
    // `destroyed` stays false.
    RGBTDivision d;
    d.OnTriggerEnter2D(false);
    EXPECT_FALSE(d.Destroyed());
}

TEST(RGBTDivisionTest, FlagLatchesOnceArmed) {
    // Once armed, a later non-matching hit does not clear it (decomp only ever
    // writes 1; it never writes 0 here).
    RGBTDivision d;
    d.OnTriggerEnter2D(true);
    d.OnTriggerEnter2D(false);
    EXPECT_TRUE(d.Destroyed());
}

// --- Division: the `-count < count` (count > 0) spawn gate --------------------

TEST(RGBTDivisionTest, HasDivisionCountMatchesCountGreaterThanZero) {
    // The decomp test is `-count < count`, i.e. count > 0.
    EXPECT_FALSE(RGBTDivision::HasDivisionCount(0));
    EXPECT_FALSE(RGBTDivision::HasDivisionCount(-1));
    EXPECT_FALSE(RGBTDivision::HasDivisionCount(-8));
    EXPECT_TRUE(RGBTDivision::HasDivisionCount(1));
    EXPECT_TRUE(RGBTDivision::HasDivisionCount(8));
}

// --- Division: the full `!destroyed && count > 0` decision -------------------

TEST(RGBTDivisionTest, ShouldDivideWhenNotDestroyedAndHasCount) {
    // !destroyed && count > 0 -> the owner spawns the fan.
    EXPECT_TRUE(RGBTDivision::ShouldDivide(false, 4));
    EXPECT_TRUE(RGBTDivision::ShouldDivide(false, 1));
}

TEST(RGBTDivisionTest, DoesNotDivideOnceDestroyed) {
    // `if (destroyed == 0)` fails -> no spawn, regardless of count.
    EXPECT_FALSE(RGBTDivision::ShouldDivide(true, 4));
    EXPECT_FALSE(RGBTDivision::ShouldDivide(true, 1));
}

TEST(RGBTDivisionTest, DoesNotDivideWithoutCount) {
    // !destroyed but count <= 0 -> the inner `-count < count` gate fails.
    EXPECT_FALSE(RGBTDivision::ShouldDivide(false, 0));
    EXPECT_FALSE(RGBTDivision::ShouldDivide(false, -3));
}

TEST(RGBTDivisionTest, InstanceShouldDivideUsesLiveFlag) {
    // The instance form combines the latched hit state with the count.
    RGBTDivision d;
    // Before any hit: !destroyed, so a positive count divides.
    EXPECT_TRUE(d.ShouldDivide(5));
    EXPECT_FALSE(d.ShouldDivide(0));
    // After a matching hit arms `destroyed`: the gate closes.
    d.OnTriggerEnter2D(true);
    EXPECT_FALSE(d.ShouldDivide(5));
}

// --- Determinism: this brain makes ZERO RNG draws ----------------------------

TEST(RGBTDivisionTest, MakesNoRngDraws) {
    // Neither OnTriggerEnter2D nor Division calls rg_random, so exercising the
    // full brain must leave the seeded stream completely un-advanced: an
    // identically seeded reference instance that did nothing must produce the
    // same next draw.
    RGBTDivision a;
    RGBTDivision reference;
    a.SetSeed(782515);
    reference.SetSeed(782515);
    EXPECT_TRUE(a.Seeded());

    // Drive every recoverable code path on 'a'.
    a.OnTriggerEnter2D(false);
    (void)RGBTDivision::HasDivisionCount(6);
    (void)RGBTDivision::ShouldDivide(false, 6);
    (void)a.ShouldDivide(6);
    a.OnTriggerEnter2D(true);
    (void)a.ShouldDivide(6);

    // If any draw had occurred on 'a', this next draw would diverge.
    EXPECT_EQ(a.Rng().Range(0, 1000000), reference.Rng().Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
