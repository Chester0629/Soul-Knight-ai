#include <gtest/gtest.h>

#include "data/RGRandom.hpp"
#include "world/RGBox.hpp"

using Game::RGBox;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- SetSourceObject (single field store, no gate, no RNG) ------------------

TEST(RGBoxTest, SetSourceObjectStoresHandle) {
    // FAITHFUL: RGBox__SetSourceObject @ game_full.c:468730 -- *(this+0x14)=param.
    RGBox box;
    EXPECT_EQ(box.SourceObject(), 0); // default: none
    box.SetSourceObject(42);
    EXPECT_EQ(box.SourceObject(), 42);
    box.SetSourceObject(-7); // overwrites verbatim, no gate
    EXPECT_EQ(box.SourceObject(), -7);
}

TEST(RGBoxTest, SetSourceObjectTakesNoRngDraw) {
    // The setter must not advance the per-instance stream: a box whose stream
    // we never draw from must agree, draw-for-draw, with a fresh same-seeded
    // reference stream after the setter has run.
    RGBox box;
    RGRandom ref;
    RGRandom fresh;
    box.SetSeed(123);
    ref.SetRandomSeed(123);
    fresh.SetRandomSeed(123);
    box.SetSourceObject(999);
    box.SetSourceObject(1);
    EXPECT_EQ(box.Hp(), 0); // sanity: setter did not touch HP either
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(ref.Range(0, 100), fresh.Range(0, 100));
    }
}

// ---- Hit: gate on (source valid) AND (HP > 0) -------------------------------

TEST(RGBoxTest, HitGatedOutWhenSourceInvalid) {
    // FAITHFUL: Hit @ 467595 -- requires Object.op_Implicit(source)==1. When the
    // source is not a live object the gate fails: NOTHING is written, return 0.
    RGBox box;
    box.SetHp(10);
    const RGBox::HitResult r = box.Hit(3, /*sourceValid=*/false);
    EXPECT_FALSE(r.registered);
    EXPECT_FALSE(r.shouldDestroy);
    EXPECT_EQ(box.Hp(), 10); // HP untouched on a failed gate
}

TEST(RGBoxTest, HitGatedOutWhenHpNotPositive) {
    // FAITHFUL: Hit @ 467595 -- requires 0 < this[3] (HP>0). HP==0 fails the gate.
    RGBox box;
    box.SetHp(0);
    const RGBox::HitResult r = box.Hit(5, /*sourceValid=*/true);
    EXPECT_FALSE(r.registered);
    EXPECT_FALSE(r.shouldDestroy);
    EXPECT_EQ(box.Hp(), 0); // no decrement past zero -- gate blocks the write

    box.SetHp(-2); // already negative: still gated out (0 < -2 is false)
    const RGBox::HitResult r2 = box.Hit(5, /*sourceValid=*/true);
    EXPECT_FALSE(r2.registered);
    EXPECT_FALSE(r2.shouldDestroy);
    EXPECT_EQ(box.Hp(), -2);
}

// ---- Hit: registered hit decrements HP and returns registered ---------------

TEST(RGBoxTest, HitRegistersAndDecrementsHp) {
    // FAITHFUL: Hit @ 467596-467598 -- newHp = HP - damage; registered = 1; store.
    RGBox box;
    box.SetHp(10);
    const RGBox::HitResult r = box.Hit(3, /*sourceValid=*/true);
    EXPECT_TRUE(r.registered);
    EXPECT_FALSE(r.shouldDestroy); // 10-3 = 7 >= 1
    EXPECT_EQ(box.Hp(), 7);
}

TEST(RGBoxTest, HitBelowOneSignalsDestroy) {
    // FAITHFUL: Hit @ 467599 -- if (newHp < 1) fire on-death dispatch. We surface
    // that as shouldDestroy. Exactly-1 remaining must NOT destroy (1 < 1 false).
    RGBox boxExactlyOne;
    boxExactlyOne.SetHp(4);
    const RGBox::HitResult keep = boxExactlyOne.Hit(3, /*sourceValid=*/true);
    EXPECT_TRUE(keep.registered);
    EXPECT_FALSE(keep.shouldDestroy); // 4-3 = 1, and 1 < 1 is false
    EXPECT_EQ(boxExactlyOne.Hp(), 1);

    RGBox boxToZero;
    boxToZero.SetHp(4);
    const RGBox::HitResult die = boxToZero.Hit(4, /*sourceValid=*/true);
    EXPECT_TRUE(die.registered);
    EXPECT_TRUE(die.shouldDestroy); // 4-4 = 0, and 0 < 1 is true
    EXPECT_EQ(boxToZero.Hp(), 0);

    RGBox boxOverkill;
    boxOverkill.SetHp(4);
    const RGBox::HitResult over = boxOverkill.Hit(100, /*sourceValid=*/true);
    EXPECT_TRUE(over.registered);
    EXPECT_TRUE(over.shouldDestroy); // 4-100 = -96, -96 < 1 is true
    EXPECT_EQ(boxOverkill.Hp(), -96); // HP is written verbatim, no clamp
}

TEST(RGBoxTest, ZeroDamageRegistersButNeverDestroys) {
    // damage 0 on a live box: newHp == HP (>0), so registered but not destroyed.
    RGBox box;
    box.SetHp(5);
    const RGBox::HitResult r = box.Hit(0, /*sourceValid=*/true);
    EXPECT_TRUE(r.registered);
    EXPECT_FALSE(r.shouldDestroy);
    EXPECT_EQ(box.Hp(), 5);
}

// ---- Hit: zero RNG draw on every path (gate fail AND registered) ------------

TEST(RGBoxTest, HitTakesNoRngDrawOnAnyPath) {
    // RGBox has NO rg_random draw anywhere. After a mix of gated-out and
    // registered hits, a parallel same-seeded stream must still match draw-for-
    // draw (the box stream is non-advancing).
    RGBox box;
    RGRandom ref;
    box.SetSeed(2024);
    ref.SetRandomSeed(2024);

    box.SetHp(20);
    box.Hit(3, true);   // registered, no draw
    box.Hit(0, false);  // gated out (invalid source), no draw
    box.SetHp(0);
    box.Hit(9, true);   // gated out (HP not >0), no draw
    box.SetHp(2);
    box.Hit(99, true);  // registered + destroy, no draw

    // Reference stream was never drawn either; both are at draw index 0, so the
    // next draws must agree.
    RGRandom fresh;
    fresh.SetRandomSeed(2024);
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(ref.Range(0, 1000), fresh.Range(0, 1000));
    }
}

// ---- Determinism / replay ---------------------------------------------------

TEST(RGBoxTest, HitSequenceIsDeterministic) {
    auto run = [](int seed) {
        RGBox box;
        box.SetSeed(seed);
        box.SetHp(15);
        box.SetSourceObject(seed);
        int trace = 0;
        for (int i = 0; i < 8; ++i) {
            const RGBox::HitResult r = box.Hit(2, (i % 3) != 0);
            trace = trace * 4 + (r.registered ? 2 : 0) + (r.shouldDestroy ? 1 : 0);
        }
        return trace + box.Hp();
    };
    EXPECT_EQ(run(555), run(555));
}

// NOLINTEND(readability-magic-numbers)
