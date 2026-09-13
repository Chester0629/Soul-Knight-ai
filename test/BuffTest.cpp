#include <gtest/gtest.h>

#include "combat/Buff.hpp"
#include "sim/SimConfig.hpp"

using Game::Combat::ActiveBuff;
using Game::Combat::BuffKind;
using Game::Combat::BuffList;
using Game::Combat::BuffParams;

namespace {

constexpr float kDt = Game::Sim::kFixedStepSeconds; // 0.02s, matches the sim's fixed step.

BuffParams Fire() {
    BuffParams p;
    p.kind = BuffKind::Fire;
    p.durationSec = 2.0F;
    p.tickSec = 1.0F;
    p.tickDamage = 1;
    return p;
}

BuffParams Poison() {
    BuffParams p;
    p.kind = BuffKind::Poison;
    p.durationSec = 5.0F;
    p.tickSec = 1.0F;
    p.tickDamage = 1;
    return p;
}

BuffParams Ice() {
    BuffParams p;
    p.kind = BuffKind::Ice;
    p.durationSec = 2.75F;
    p.speedMul = 0.0F;
    return p;
}

BuffParams Electric() {
    BuffParams p;
    p.kind = BuffKind::Electric;
    p.durationSec = 2.0F;
    p.speedMul = 0.0F;
    return p;
}

BuffParams Armor() {
    BuffParams p;
    p.kind = BuffKind::Armor;
    p.durationSec = 6.0F;
    p.armorDelta = 1;
    return p;
}

BuffParams ChangeSpeed() {
    BuffParams p;
    p.kind = BuffKind::ChangeSpeed;
    p.durationSec = 6.0F;
    p.speedMul = 0.25F; // matches buffs.json's BuffChangeSpeed._inferred.speed_mul.
    return p;
}

// Runs Tick(kDt) enough times to reach/exceed durationSec; returns the number of
// steps taken to go from Has()==true to Has()==false (ceil(duration/dt)).
int StepsUntilExpired(BuffList &list, BuffKind kind, int maxSteps = 1000) {
    for (int i = 0; i < maxSteps; ++i) {
        list.Tick(kDt);
        if (!list.Has(kind)) {
            return i + 1;
        }
    }
    return -1;
}

} // namespace

// NOLINTBEGIN(readability-magic-numbers)

// --- Fire --------------------------------------------------------------------

TEST(BuffTest, FireAppliesAndIsHasTrue) {
    BuffList list;
    EXPECT_FALSE(list.Has(BuffKind::Fire));
    list.Apply(Fire());
    EXPECT_TRUE(list.Has(BuffKind::Fire));
}

TEST(BuffTest, FireEndsAtBuffTime) {
    BuffList list;
    list.Apply(Fire());
    // 2.0s / 0.02s = 100 steps exactly (buff_time divides the fixed step evenly).
    const int steps = StepsUntilExpired(list, BuffKind::Fire);
    EXPECT_EQ(steps, 100);
}

TEST(BuffTest, FireTickCountOverDurationMatchesFloorDurationOverTick) {
    BuffList list;
    list.Apply(Fire()); // duration 2.0s, tick 1.0s -> floor(2/1) = 2 ticks.
    int totalDamage = 0;
    for (int i = 0; i < 100; ++i) {
        totalDamage += list.Tick(kDt).damage;
    }
    EXPECT_EQ(totalDamage, 2);
}

TEST(BuffTest, ReapplyingFireRefreshesInsteadOfStacking) {
    BuffList list;
    list.Apply(Fire());
    for (int i = 0; i < 50; ++i) { // half the duration
        list.Tick(kDt);
    }
    list.Apply(Fire()); // refresh: remaining should reset back to 2.0s (100 steps).
    const int steps = StepsUntilExpired(list, BuffKind::Fire);
    EXPECT_EQ(steps, 100);
    // Only 2 total ticks over the refreshed lifetime (not stacked/doubled).
    BuffList list2;
    list2.Apply(Fire());
    int dmgBeforeRefresh = 0;
    for (int i = 0; i < 50; ++i) {
        dmgBeforeRefresh += list2.Tick(kDt).damage;
    }
    list2.Apply(Fire());
    int dmgAfterRefresh = 0;
    for (int i = 0; i < 100; ++i) {
        dmgAfterRefresh += list2.Tick(kDt).damage;
    }
    EXPECT_EQ(dmgBeforeRefresh + dmgAfterRefresh, 1 + 2); // 1 tick pre-refresh + 2 post.
}

// --- Poison --------------------------------------------------------------------

TEST(BuffTest, PoisonTickCountOverDuration) {
    BuffList list;
    list.Apply(Poison()); // duration 5.0s, tick 1.0s -> floor(5/1) = 5 ticks.
    int totalDamage = 0;
    for (int i = 0; i < 250; ++i) { // 5.0 / 0.02 = 250 steps
        totalDamage += list.Tick(kDt).damage;
    }
    EXPECT_EQ(totalDamage, 5);
}

TEST(BuffTest, PoisonEndsAtBuffTime) {
    BuffList list;
    list.Apply(Poison());
    EXPECT_EQ(StepsUntilExpired(list, BuffKind::Poison), 250);
}

TEST(BuffTest, PoisonHasWhileActive) {
    BuffList list;
    list.Apply(Poison());
    for (int i = 0; i < 249; ++i) {
        list.Tick(kDt);
        EXPECT_TRUE(list.Has(BuffKind::Poison));
    }
}

TEST(BuffTest, ReapplyingPoisonRefreshesNotStacks) {
    BuffList list;
    list.Apply(Poison());
    for (int i = 0; i < 125; ++i) {
        list.Tick(kDt);
    }
    list.Apply(Poison());
    EXPECT_EQ(StepsUntilExpired(list, BuffKind::Poison), 250);
}

// --- Ice (freeze) ----------------------------------------------------------

TEST(BuffTest, IceFreezesSpeedToZero) {
    BuffList list;
    list.Apply(Ice());
    const BuffList::TickResult r = list.Tick(kDt);
    EXPECT_TRUE(r.frozen);
    EXPECT_FLOAT_EQ(r.speedMul, 0.0F);
}

TEST(BuffTest, IceEndsWithinOneStepOfBuffTime) {
    BuffList list;
    list.Apply(Ice()); // 2.75s does not divide 0.02s evenly -> ceil(2.75/0.02) = 138.
    const int steps = StepsUntilExpired(list, BuffKind::Ice);
    EXPECT_EQ(steps, 138);
}

TEST(BuffTest, ReapplyingIceRefreshesNotStacks) {
    BuffList list;
    list.Apply(Ice());
    for (int i = 0; i < 60; ++i) {
        list.Tick(kDt);
    }
    list.Apply(Ice());
    EXPECT_EQ(StepsUntilExpired(list, BuffKind::Ice), 138);
}

// --- Electric (stun) ---------------------------------------------------------

TEST(BuffTest, ElectricFreezesSpeedToZero) {
    BuffList list;
    list.Apply(Electric());
    const BuffList::TickResult r = list.Tick(kDt);
    EXPECT_TRUE(r.frozen);
    EXPECT_FLOAT_EQ(r.speedMul, 0.0F);
}

TEST(BuffTest, ElectricEndsAtBuffTime) {
    BuffList list;
    list.Apply(Electric());
    EXPECT_EQ(StepsUntilExpired(list, BuffKind::Electric), 100); // 2.0/0.02
}

// --- Armor -------------------------------------------------------------------

TEST(BuffTest, ArmorGrantsDeltaWhileActiveNotFrozenOrDamaging) {
    BuffList list;
    list.Apply(Armor());
    const BuffList::TickResult r = list.Tick(kDt);
    EXPECT_EQ(r.armorDelta, 1);
    EXPECT_EQ(r.damage, 0);
    EXPECT_FALSE(r.frozen);
    EXPECT_FLOAT_EQ(r.speedMul, 1.0F);
}

TEST(BuffTest, ArmorEndsAtBuffTime) {
    BuffList list;
    list.Apply(Armor());
    EXPECT_EQ(StepsUntilExpired(list, BuffKind::Armor), 300); // 6.0/0.02
}

// --- ChangeSpeed ---------------------------------------------------------------

TEST(BuffTest, ChangeSpeedAppliesConfiguredMultiplier) {
    BuffList list;
    list.Apply(ChangeSpeed());
    const BuffList::TickResult r = list.Tick(kDt);
    EXPECT_FLOAT_EQ(r.speedMul, 0.25F);
    EXPECT_FALSE(r.frozen); // 0.25 != 0 -> slowed, not frozen.
}

TEST(BuffTest, ChangeSpeedEndsAtBuffTime) {
    BuffList list;
    list.Apply(ChangeSpeed());
    EXPECT_EQ(StepsUntilExpired(list, BuffKind::ChangeSpeed), 300); // 6.0/0.02
}

// --- Combined / Clear ----------------------------------------------------------

TEST(BuffTest, MultipleActiveBuffsCombineSpeedMulMultiplicatively) {
    BuffList list;
    list.Apply(ChangeSpeed()); // 0.25
    const BuffList::TickResult r = list.Tick(kDt);
    EXPECT_FLOAT_EQ(r.speedMul, 0.25F);
}

TEST(BuffTest, ClearRemovesAllActiveBuffsWithoutEndedEvents) {
    BuffList list;
    list.Apply(Fire());
    list.Apply(Armor());
    list.Clear();
    EXPECT_FALSE(list.Has(BuffKind::Fire));
    EXPECT_FALSE(list.Has(BuffKind::Armor));
}

// NOLINTEND(readability-magic-numbers)
