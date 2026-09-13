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

// Fire-shaped DoT whose duration is NOT a multiple of tickSec (and does not divide
// evenly into the 0.02s fixed step): floor(2.75 / 1.0) = 2 ticks, never 3.
BuffParams FireNonMultipleDuration() {
    BuffParams p;
    p.kind = BuffKind::Fire;
    p.durationSec = 2.75F;
    p.tickSec = 1.0F;
    p.tickDamage = 1;
    return p;
}

// ChangeSpeed with a SHORTER life than Fire's, for the two-kinds-at-once test.
BuffParams ChangeSpeedLong() {
    BuffParams p;
    p.kind = BuffKind::ChangeSpeed;
    p.durationSec = 6.0F;
    p.speedMul = 0.25F;
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

/// Outcome of ticking one BuffList until `kind` is gone: how many steps it took,
/// whether the ENDING Tick()'s `ended` vector actually named that kind, and the
/// total tick damage emitted over the buff's whole lifetime.
struct ExpiryProbe {
    int steps = -1;
    bool endedVectorHadKind = false;
    int totalDamage = 0;
};

// Loops while Has(kind) is true (never a fixed iteration count), so the probe itself
// proves no extra tick is emitted after expiry.
ExpiryProbe RunUntilExpired(BuffList &list, BuffKind kind, int maxSteps = 1000) {
    ExpiryProbe out;
    for (int i = 0; i < maxSteps; ++i) {
        const BuffList::TickResult r = list.Tick(kDt);
        out.totalDamage += r.damage;
        if (!list.Has(kind)) {
            out.steps = i + 1;
            for (const BuffKind k : r.ended) {
                if (k == kind) {
                    out.endedVectorHadKind = true;
                }
            }
            return out;
        }
    }
    return out;
}

// One Tick() on a list whose every buff has already expired: nothing may linger.
void ExpectFullyRestored(BuffList &list) {
    const BuffList::TickResult r = list.Tick(kDt);
    EXPECT_EQ(r.damage, 0);
    EXPECT_FLOAT_EQ(r.speedMul, 1.0F);
    EXPECT_FALSE(r.frozen);
    EXPECT_EQ(r.armorDelta, 0);
    EXPECT_TRUE(r.ended.empty());
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
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Fire);
    EXPECT_EQ(probe.steps, 100);
    EXPECT_TRUE(probe.endedVectorHadKind) << "the ending Tick() must report Fire in r.ended";
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

// Exact-multiple duration, run to FULL expiry via Has() (not a fixed loop count):
// 2.0 / 1.0 = 2 ticks and not one more on the step that ends the buff.
TEST(BuffTest, FireExactMultipleDurationTicksExactlyDurationOverTickSec) {
    BuffList list;
    list.Apply(Fire());
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Fire);
    EXPECT_EQ(probe.steps, 100);
    EXPECT_EQ(probe.totalDamage, 2);
    EXPECT_TRUE(probe.endedVectorHadKind);
}

// Regression: durationSec (2.75s) is NOT a multiple of tickSec (1.0s) and does not
// land on a fixed-step boundary. Only in-lifetime time may be credited to tickAccum,
// so the count is floor(2.75 / 1.0) = 2 -- never 3.
TEST(BuffTest, NonMultipleDurationTicksFloorDurationOverTickSecNeverOneMore) {
    BuffList list;
    list.Apply(FireNonMultipleDuration());
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Fire);
    EXPECT_EQ(probe.steps, 138); // ceil(2.75 / 0.02) = 137.5 -> 138
    EXPECT_EQ(probe.totalDamage, 2);
    EXPECT_TRUE(probe.endedVectorHadKind);
    ExpectFullyRestored(list); // no phantom tick after the buff is gone.
}

TEST(BuffTest, FireFullyRestoredAfterExpiry) {
    BuffList list;
    list.Apply(Fire());
    RunUntilExpired(list, BuffKind::Fire);
    EXPECT_FALSE(list.Has(BuffKind::Fire));
    ExpectFullyRestored(list);
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
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Poison);
    EXPECT_EQ(probe.steps, 250);
    EXPECT_EQ(probe.totalDamage, 5); // floor(5.0 / 1.0), asserted over the full lifetime.
    EXPECT_TRUE(probe.endedVectorHadKind);
}

TEST(BuffTest, PoisonFullyRestoredAfterExpiry) {
    BuffList list;
    list.Apply(Poison());
    RunUntilExpired(list, BuffKind::Poison);
    EXPECT_FALSE(list.Has(BuffKind::Poison));
    ExpectFullyRestored(list);
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
    int dmgBeforeRefresh = 0;
    for (int i = 0; i < 125; ++i) { // 2.5s -> 2 ticks so far
        dmgBeforeRefresh += list.Tick(kDt).damage;
    }
    EXPECT_EQ(dmgBeforeRefresh, 2);
    list.Apply(Poison());
    // Remaining time resets to the full 250 steps...
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Poison);
    EXPECT_EQ(probe.steps, 250);
    // ...and the refreshed instance ticks 5 times, not 10 (no second stacked instance).
    EXPECT_EQ(probe.totalDamage, 5);
    EXPECT_TRUE(probe.endedVectorHadKind);
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
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Ice);
    EXPECT_EQ(probe.steps, 138);
    EXPECT_TRUE(probe.endedVectorHadKind);
}

TEST(BuffTest, ReapplyingIceRefreshesNotStacks) {
    BuffList list;
    list.Apply(Ice());
    for (int i = 0; i < 60; ++i) {
        list.Tick(kDt);
    }
    list.Apply(Ice());
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Ice);
    EXPECT_EQ(probe.steps, 138);
    EXPECT_TRUE(probe.endedVectorHadKind);
    // A stacked second instance would leave a 0.0 multiplier behind; it does not.
    ExpectFullyRestored(list);
}

TEST(BuffTest, IceFullyRestoredAfterExpiry) {
    BuffList list;
    list.Apply(Ice());
    RunUntilExpired(list, BuffKind::Ice);
    EXPECT_FALSE(list.Has(BuffKind::Ice));
    ExpectFullyRestored(list); // speedMul back to 1.0, frozen back to false.
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
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Electric); // 2.0/0.02
    EXPECT_EQ(probe.steps, 100);
    EXPECT_TRUE(probe.endedVectorHadKind);
}

TEST(BuffTest, ReapplyingElectricRefreshesNotStacks) {
    BuffList list;
    list.Apply(Electric());
    for (int i = 0; i < 50; ++i) { // half the 2.0s life
        list.Tick(kDt);
    }
    list.Apply(Electric()); // refresh: remaining resets to the full 100 steps.
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Electric);
    EXPECT_EQ(probe.steps, 100);
    EXPECT_TRUE(probe.endedVectorHadKind);
    // One instance only: a stacked second copy would keep the freeze alive here.
    ExpectFullyRestored(list);
}

TEST(BuffTest, ElectricFullyRestoredAfterExpiry) {
    BuffList list;
    list.Apply(Electric());
    RunUntilExpired(list, BuffKind::Electric);
    EXPECT_FALSE(list.Has(BuffKind::Electric));
    ExpectFullyRestored(list); // speedMul back to 1.0, frozen back to false.
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
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Armor); // 6.0/0.02
    EXPECT_EQ(probe.steps, 300);
    EXPECT_TRUE(probe.endedVectorHadKind);
}

TEST(BuffTest, ReapplyingArmorRefreshesNotStacks) {
    BuffList list;
    list.Apply(Armor());
    for (int i = 0; i < 150; ++i) { // half the 6.0s life
        list.Tick(kDt);
    }
    list.Apply(Armor());
    // Refreshing does NOT stack a second +1: the reported delta stays 1.
    EXPECT_EQ(list.Tick(kDt).armorDelta, 1);
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::Armor);
    EXPECT_EQ(probe.steps, 299); // 300 steps of refreshed life minus the probe Tick above.
    EXPECT_TRUE(probe.endedVectorHadKind);
    ExpectFullyRestored(list);
}

TEST(BuffTest, ArmorFullyRestoredAfterExpiry) {
    BuffList list;
    list.Apply(Armor());
    RunUntilExpired(list, BuffKind::Armor);
    EXPECT_FALSE(list.Has(BuffKind::Armor));
    ExpectFullyRestored(list); // armorDelta contributes 0 once the buff is gone.
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
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::ChangeSpeed); // 6.0/0.02
    EXPECT_EQ(probe.steps, 300);
    EXPECT_TRUE(probe.endedVectorHadKind);
}

TEST(BuffTest, ReapplyingChangeSpeedRefreshesNotStacks) {
    BuffList list;
    list.Apply(ChangeSpeed());
    for (int i = 0; i < 150; ++i) { // half the 6.0s life
        list.Tick(kDt);
    }
    list.Apply(ChangeSpeed());
    // A stacked second instance would multiply to 0.0625; refreshing keeps 0.25.
    EXPECT_FLOAT_EQ(list.Tick(kDt).speedMul, 0.25F);
    const ExpiryProbe probe = RunUntilExpired(list, BuffKind::ChangeSpeed);
    EXPECT_EQ(probe.steps, 299); // 300 steps of refreshed life minus the probe Tick above.
    EXPECT_TRUE(probe.endedVectorHadKind);
    ExpectFullyRestored(list);
}

TEST(BuffTest, ChangeSpeedFullyRestoredAfterExpiry) {
    BuffList list;
    list.Apply(ChangeSpeed());
    RunUntilExpired(list, BuffKind::ChangeSpeed);
    EXPECT_FALSE(list.Has(BuffKind::ChangeSpeed));
    ExpectFullyRestored(list); // speedMul back to 1.0.
}

// --- Combined / Clear ----------------------------------------------------------

// Every kind is Has()==true the instant it is applied, before any Tick().
TEST(BuffTest, EveryKindIsHasTrueImmediatelyAfterApply) {
    const BuffParams all[] = {Fire(), Poison(), Ice(), Electric(), Armor(), ChangeSpeed()};
    for (const BuffParams &p : all) {
        BuffList list;
        EXPECT_FALSE(list.Has(p.kind));
        list.Apply(p);
        EXPECT_TRUE(list.Has(p.kind)) << "kind " << Game::Combat::BuffKindName(p.kind);
    }
}

// Two DIFFERENT kinds on one list coexist: both effects show up in the SAME
// TickResult, and they are tracked independently (the shorter one expiring does
// not disturb the longer one).
TEST(BuffTest, MultipleActiveBuffsOfDifferentKindsCoexist) {
    BuffList list;
    list.Apply(Fire());           // 2.0s, 1 dmg per 1.0s tick
    list.Apply(ChangeSpeedLong()); // 6.0s, speedMul 0.25
    EXPECT_TRUE(list.Has(BuffKind::Fire));
    EXPECT_TRUE(list.Has(BuffKind::ChangeSpeed));

    // Step 50 = 1.0s: Fire's first tick and ChangeSpeed's multiplier in one result.
    int damageSoFar = 0;
    for (int i = 0; i < 50; ++i) {
        const BuffList::TickResult r = list.Tick(kDt);
        damageSoFar += r.damage;
        EXPECT_FLOAT_EQ(r.speedMul, 0.25F); // ChangeSpeed active the whole time
        EXPECT_FALSE(r.frozen);
    }
    EXPECT_EQ(damageSoFar, 1); // Fire ticked once while ChangeSpeed was also active.

    // Run to Fire's expiry (step 100 overall); ChangeSpeed must survive it.
    const ExpiryProbe fireProbe = RunUntilExpired(list, BuffKind::Fire);
    EXPECT_EQ(fireProbe.steps, 50);
    EXPECT_EQ(damageSoFar + fireProbe.totalDamage, 2); // floor(2.0/1.0) total
    EXPECT_TRUE(fireProbe.endedVectorHadKind);
    EXPECT_TRUE(list.Has(BuffKind::ChangeSpeed));

    // ChangeSpeed alone now: still slowing, no more Fire damage.
    const BuffList::TickResult after = list.Tick(kDt);
    EXPECT_FLOAT_EQ(after.speedMul, 0.25F);
    EXPECT_EQ(after.damage, 0);

    // And it ends on its own schedule: 300 steps total, 101 already spent.
    const ExpiryProbe csProbe = RunUntilExpired(list, BuffKind::ChangeSpeed);
    EXPECT_EQ(csProbe.steps, 199);
    EXPECT_TRUE(csProbe.endedVectorHadKind);
    ExpectFullyRestored(list);
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
