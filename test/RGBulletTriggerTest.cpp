#include <gtest/gtest.h>

#include "combat/RGBulletTrigger.hpp"

using Game::RGBulletTrigger;

// NOLINTBEGIN(readability-magic-numbers)

// --- through_count: budget + can_through invariant ---------------------------

TEST(RGBulletTriggerTest, DefaultsAreInert) {
    RGBulletTrigger t;
    // No SetInfo yet: budget 0, cannot pierce, not flagged for destruction.
    EXPECT_EQ(t.GetThroughCount(), 0);
    EXPECT_FALSE(t.CanThrough());
    EXPECT_FALSE(t.NeedDestroy());
}

TEST(RGBulletTriggerTest, SetThroughCountDerivesCanThrough) {
    RGBulletTrigger t;
    // set_through_count: *(0x18) = v ; *(0x14) = (0 < v).
    t.SetThroughCount(3);
    EXPECT_EQ(t.GetThroughCount(), 3);
    EXPECT_TRUE(t.CanThrough()); // 0 < 3

    t.SetThroughCount(0);
    EXPECT_EQ(t.GetThroughCount(), 0);
    EXPECT_FALSE(t.CanThrough()); // 0 < 0 is false
}

TEST(RGBulletTriggerTest, NegativeBudgetIsNotPiercing) {
    RGBulletTrigger t;
    // The decomp predicate is a strict `0 < v`, so a negative budget is non-pierce.
    t.SetThroughCount(-5);
    EXPECT_EQ(t.GetThroughCount(), -5);
    EXPECT_FALSE(t.CanThrough());
}

TEST(RGBulletTriggerTest, InfiniteBudgetSentinelIsPiercing) {
    RGBulletTrigger t;
    // 0xff is SetInfo's infinite-pierce sentinel; still just 0 < 0xff -> true.
    t.SetThroughCount(RGBulletTrigger::kInfinitePierceBudget);
    EXPECT_EQ(t.GetThroughCount(), 0xff);
    EXPECT_TRUE(t.CanThrough());
}

// --- SetInfo(bool): bool -> budget conversion --------------------------------

TEST(RGBulletTriggerTest, ThroughBudgetFromBoolMatchesDecomp) {
    // `uVar1 = 0; if (param_4 != 0) uVar1 = 0xff;`
    EXPECT_EQ(RGBulletTrigger::ThroughBudgetFromBool(false), 0);
    EXPECT_EQ(RGBulletTrigger::ThroughBudgetFromBool(true), 0xff);
    EXPECT_EQ(RGBulletTrigger::ThroughBudgetFromBool(true),
              RGBulletTrigger::kInfinitePierceBudget);
}

TEST(RGBulletTriggerTest, ApplySetInfoThroughStoresBudgetAndFlag) {
    RGBulletTrigger t;
    // SetInfo(true): budget = 0xff, can_through derived true.
    t.ApplySetInfoThrough(true);
    EXPECT_EQ(t.GetThroughCount(), 0xff);
    EXPECT_TRUE(t.CanThrough());

    // SetInfo(false): budget = 0, can_through derived false.
    t.ApplySetInfoThrough(false);
    EXPECT_EQ(t.GetThroughCount(), 0);
    EXPECT_FALSE(t.CanThrough());
}

// --- GetDamageFactor ---------------------------------------------------------

TEST(RGBulletTriggerTest, DamageFactorIsOneWithoutIceBuff) {
    // Recovered non-ice return: 0x3f800000 == 1.0f.
    EXPECT_FLOAT_EQ(RGBulletTrigger::GetDamageFactor(false), 1.0F);
    EXPECT_FLOAT_EQ(RGBulletTrigger::GetDamageFactor(false),
                    RGBulletTrigger::kNoBuffDamageFactor);
}

TEST(RGBulletTriggerTest, DamageFactorIceBranchIsOwnerSideBaseline) {
    // has_ice_buff branch truncates in the decomp (Singleton<RGGameProcess> tail).
    // We never fabricate the ice factor; the recovered baseline stays 1.0f, and
    // the branch-taken flag is reported for the owner to resolve.
    EXPECT_TRUE(RGBulletTrigger::TakesIceBranch(true));
    EXPECT_FALSE(RGBulletTrigger::TakesIceBranch(false));
    EXPECT_FLOAT_EQ(RGBulletTrigger::GetDamageFactor(true), 1.0F);
    // The unverified ice constant is NOT the return value -- it is documentation only.
    EXPECT_NE(RGBulletTrigger::GetDamageFactor(true),
              RGBulletTrigger::kIceBuffFactorUnverified);
}

// --- OnTriggerEnter2D gate head ----------------------------------------------

TEST(RGBulletTriggerTest, TriggerHeadClearsNeedDestroy) {
    RGBulletTrigger t;
    // The only recovered statement: need_destory = false at entry.
    t.OnTriggerEnterHead();
    EXPECT_FALSE(t.NeedDestroy());
    // Idempotent: re-entry keeps it cleared (the owner sets it later, off-port).
    t.OnTriggerEnterHead();
    EXPECT_FALSE(t.NeedDestroy());
}

// --- DestroyBullet branch predicates -----------------------------------------

TEST(RGBulletTriggerTest, DestroyBulletPredicatesMatchPackedFlags) {
    // low byte 0x30 (destory_parent) -> spawn VFX branch.
    EXPECT_TRUE(RGBulletTrigger::DestroyBulletSpawnsVfx(true));
    EXPECT_FALSE(RGBulletTrigger::DestroyBulletSpawnsVfx(false));
    // high byte 0x31 (stop_parent), `0xff < ushort` -> parent-stop branch.
    EXPECT_TRUE(RGBulletTrigger::DestroyBulletStopsParent(true));
    EXPECT_FALSE(RGBulletTrigger::DestroyBulletStopsParent(false));
}

// --- Determinism: this unit makes ZERO RNG draws -----------------------------

TEST(RGBulletTriggerTest, MakesNoRngDraws) {
    // No recovered RGBulletTrigger body calls rg_random (the crit roll lives in the
    // truncated owner-side OnTriggerEnter2D dispatch). Driving every ported path
    // must leave the seeded stream completely un-advanced.
    RGBulletTrigger a;
    RGBulletTrigger reference;
    a.SetSeed(20240608);
    reference.SetSeed(20240608);
    EXPECT_TRUE(a.Seeded());

    // Exercise every recoverable code path on 'a'.
    a.SetThroughCount(4);
    a.ApplySetInfoThrough(true);
    a.ApplySetInfoThrough(false);
    (void)RGBulletTrigger::ThroughBudgetFromBool(true);
    (void)RGBulletTrigger::GetDamageFactor(false);
    (void)RGBulletTrigger::GetDamageFactor(true);
    (void)RGBulletTrigger::TakesIceBranch(true);
    a.OnTriggerEnterHead();
    (void)RGBulletTrigger::DestroyBulletSpawnsVfx(true);
    (void)RGBulletTrigger::DestroyBulletStopsParent(true);

    // If any draw had occurred on 'a', this next draw would diverge.
    EXPECT_EQ(a.Rng().Range(0, 1000000), reference.Rng().Range(0, 1000000));
}

// NOLINTEND(readability-magic-numbers)
