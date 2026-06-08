#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "combat/GunMultiBullet.hpp"

using Game::GunMultiBullet;

// NOLINTBEGIN(readability-magic-numbers)

// GunMultiBullet has NO RGRandom draw: all four getters are pure array-vs-scalar
// selectors. These tests pin the shared decision (UsesOverride) and each getter's
// override-element vs scalar-fallback behaviour, including the byte-stride pierce
// path and the equal-count + in-range gate.

// ---- UsesOverride: equal-count AND in-range selects the override path -------

TEST(GunMultiBulletTest, UsesOverrideWhenCountsEqualAndIndexInRange) {
    // override array of 3 elements, bullet-count array of 3, index 0..2 -> true.
    EXPECT_TRUE(GunMultiBullet::UsesOverride(3, 3, 0));
    EXPECT_TRUE(GunMultiBullet::UsesOverride(3, 3, 1));
    EXPECT_TRUE(GunMultiBullet::UsesOverride(3, 3, 2));
}

TEST(GunMultiBulletTest, FallsBackWhenCountsDiffer) {
    // The decomp's equal-count test (overrideArray[0xc] == bulletCount[0xc])
    // fails -> scalar branch, regardless of index.
    EXPECT_FALSE(GunMultiBullet::UsesOverride(2, 3, 0));
    EXPECT_FALSE(GunMultiBullet::UsesOverride(4, 3, 1));
    EXPECT_FALSE(GunMultiBullet::UsesOverride(0, 3, 0));
}

TEST(GunMultiBulletTest, FallsBackWhenIndexOutOfRange) {
    // Equal counts but index == count is out of range; the original would throw
    // before reaching the scalar branch, so a valid bullet index never lands
    // here -- we treat out-of-range as not-override (the scalar fallback).
    EXPECT_FALSE(GunMultiBullet::UsesOverride(3, 3, 3));
    EXPECT_FALSE(GunMultiBullet::UsesOverride(3, 3, 99));
}

TEST(GunMultiBulletTest, FallsBackWhenBothArraysEmpty) {
    // 0 == 0 counts are "equal", but index 0 is not < 0 -> scalar fallback.
    EXPECT_FALSE(GunMultiBullet::UsesOverride(0, 0, 0));
}

// ---- GetAttack: indexed int override (index*4 stride) else scalar -----------

TEST(GunMultiBulletTest, AttackReturnsIndexedOverrideWhenCountsMatch) {
    const std::vector<int> over = {11, 22, 33};
    EXPECT_EQ(GunMultiBullet::GetAttack(over, 3, 0, 99), 11);
    EXPECT_EQ(GunMultiBullet::GetAttack(over, 3, 1, 99), 22);
    EXPECT_EQ(GunMultiBullet::GetAttack(over, 3, 2, 99), 33);
}

TEST(GunMultiBulletTest, AttackReturnsScalarWhenCountMismatch) {
    const std::vector<int> over = {11, 22};
    // override length 2 != bullet count 3 -> scalar fallback (owner+0x20).
    EXPECT_EQ(GunMultiBullet::GetAttack(over, 3, 0, 99), 99);
    EXPECT_EQ(GunMultiBullet::GetAttack(over, 3, 1, 99), 99);
}

TEST(GunMultiBulletTest, AttackReturnsScalarWhenOverrideEmpty) {
    const std::vector<int> over = {};
    EXPECT_EQ(GunMultiBullet::GetAttack(over, 3, 0, 77), 77);
}

// ---- GetSpeed: indexed float override else scalar (asymmetric widening) ------

TEST(GunMultiBulletTest, SpeedReturnsIndexedOverrideWhenCountsMatch) {
    const std::vector<float> over = {1.5F, 2.5F, 3.5F};
    EXPECT_FLOAT_EQ(GunMultiBullet::GetSpeed(over, 3, 0, 9.9F), 1.5F);
    EXPECT_FLOAT_EQ(GunMultiBullet::GetSpeed(over, 3, 2, 9.9F), 3.5F);
}

TEST(GunMultiBulletTest, SpeedReturnsScalarWhenCountMismatch) {
    const std::vector<float> over = {1.5F};
    EXPECT_FLOAT_EQ(GunMultiBullet::GetSpeed(over, 3, 0, 9.9F), 9.9F);
}

// ---- GetCanThrough: indexed BYTE override (stride 1) else scalar -------------

TEST(GunMultiBulletTest, CanThroughReturnsIndexedOverrideWhenCountsMatch) {
    const std::vector<bool> over = {true, false, true};
    EXPECT_TRUE(GunMultiBullet::GetCanThrough(over, 3, 0, false));
    EXPECT_FALSE(GunMultiBullet::GetCanThrough(over, 3, 1, false));
    EXPECT_TRUE(GunMultiBullet::GetCanThrough(over, 3, 2, false));
}

TEST(GunMultiBulletTest, CanThroughReturnsScalarWhenCountMismatch) {
    const std::vector<bool> over = {true, true};
    // override length 2 != bullet count 3 -> scalar fallback (owner+0x38).
    EXPECT_TRUE(GunMultiBullet::GetCanThrough(over, 3, 0, true));
    EXPECT_FALSE(GunMultiBullet::GetCanThrough(over, 3, 1, false));
}

// ---- GetCritics: indexed int override (index*4 stride) else scalar -----------

TEST(GunMultiBulletTest, CriticsReturnsIndexedOverrideWhenCountsMatch) {
    const std::vector<int> over = {5, 50, 500};
    EXPECT_EQ(GunMultiBullet::GetCritics(over, 3, 0, -1), 5);
    EXPECT_EQ(GunMultiBullet::GetCritics(over, 3, 1, -1), 50);
    EXPECT_EQ(GunMultiBullet::GetCritics(over, 3, 2, -1), 500);
}

TEST(GunMultiBulletTest, CriticsReturnsScalarWhenCountMismatch) {
    const std::vector<int> over = {5, 50, 500, 5000};
    // override length 4 != bullet count 3 -> scalar fallback (owner+0x2c).
    EXPECT_EQ(GunMultiBullet::GetCritics(over, 3, 0, -1), -1);
}

// ---- all four selectors agree on the SAME gate at the boundary --------------

TEST(GunMultiBulletTest, AllSelectorsShareTheSameEqualCountGate) {
    // count 4 == 4 and index 3 in range -> every getter takes the override path.
    const std::vector<int> atk = {1, 2, 3, 4};
    const std::vector<float> spd = {1.0F, 2.0F, 3.0F, 4.0F};
    const std::vector<bool> thr = {false, false, false, true};
    const std::vector<int> crit = {10, 20, 30, 40};
    const std::size_t count = 4;
    const std::size_t idx = 3;
    EXPECT_EQ(GunMultiBullet::GetAttack(atk, count, idx, -9), 4);
    EXPECT_FLOAT_EQ(GunMultiBullet::GetSpeed(spd, count, idx, -9.0F), 4.0F);
    EXPECT_TRUE(GunMultiBullet::GetCanThrough(thr, count, idx, false));
    EXPECT_EQ(GunMultiBullet::GetCritics(crit, count, idx, -9), 40);

    // bump bullet count to 5 (mismatch) -> every getter falls to its scalar.
    const std::size_t mismatch = 5;
    EXPECT_EQ(GunMultiBullet::GetAttack(atk, mismatch, idx, -9), -9);
    EXPECT_FLOAT_EQ(GunMultiBullet::GetSpeed(spd, mismatch, idx, -9.0F), -9.0F);
    EXPECT_FALSE(GunMultiBullet::GetCanThrough(thr, mismatch, idx, false));
    EXPECT_EQ(GunMultiBullet::GetCritics(crit, mismatch, idx, -9), -9);
}

// NOLINTEND(readability-magic-numbers)
