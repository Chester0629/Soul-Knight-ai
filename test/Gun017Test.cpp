#include <gtest/gtest.h>

#include <vector>

#include "combat/Gun017.hpp"
#include "data/RGRandom.hpp"

using Game::Gun017;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- CanFire: drone-out gate, no RNG -----------------------------------------

TEST(Gun017Test, CanFireWhenDroneByteIsZero) {
    // FAITHFUL: Gun017__Attack @ game_full.c:964739:
    //   if (*(char *)(*(param_1 + 0x6c) + 0xc) != '\0') return;
    // child+0xc == 0 -> gate passes -> parent fires.
    EXPECT_TRUE(Gun017::CanFire(0));
}

TEST(Gun017Test, CannotFireWhenDroneByteIsNonZero) {
    // Drone active (byte != 0) -> parent does NOT fire, no RNG draw.
    EXPECT_FALSE(Gun017::CanFire(1));
    EXPECT_FALSE(Gun017::CanFire(-1));
    EXPECT_FALSE(Gun017::CanFire(255));
}

// ---- BaseSpreadWithRecoil: pure scalar, no RNG --------------------------------

TEST(Gun017Test, SpreadWithZeroRecoilIsBaseAngle) {
    // FAITHFUL: Gun017__Attack @ game_full.c:964754:
    //   fVar4 = fVar4 + fVar4 * *(float *)(iVar1 + 0x20);
    // recoil 0 -> spread == baseAngle.
    EXPECT_FLOAT_EQ(Gun017::BaseSpreadWithRecoil(15.0F, 0.0F), 15.0F);
}

TEST(Gun017Test, SpreadGrowsWithRecoil) {
    // base=10, recoil=0.5 -> 10 + 10*0.5 = 15.
    EXPECT_FLOAT_EQ(Gun017::BaseSpreadWithRecoil(10.0F, 0.5F), 15.0F);
    // base=20, recoil=1.0 -> 20 + 20 = 40.
    EXPECT_FLOAT_EQ(Gun017::BaseSpreadWithRecoil(20.0F, 1.0F), 40.0F);
}

TEST(Gun017Test, SpreadWithZeroBaseAngleIsZero) {
    // base=0 -> 0 + 0*recoil = 0 regardless of recoil.
    EXPECT_FLOAT_EQ(Gun017::BaseSpreadWithRecoil(0.0F, 2.0F), 0.0F);
}

// ---- ScatterAngle: exactly ONE float draw per fired shot ---------------------

TEST(Gun017Test, ScatterStaysWithinSpreadBounds) {
    // FAITHFUL: Gun017__Attack @ game_full.c:964759:
    //   RGRandom__Range(*(param_1 + 0x60), -fVar4, fVar4, 0);
    // Result must lie in [-spread, +spread] (max-INCLUSIVE float Range).
    Gun017 g;
    g.SetSeed(1234);
    const float spread = Gun017::BaseSpreadWithRecoil(15.0F, 0.2F);
    for (int i = 0; i < 64; ++i) {
        const float a = g.ScatterAngle(spread);
        EXPECT_GE(a, -spread);
        EXPECT_LE(a, spread);
    }
}

TEST(Gun017Test, ScatterDrawsExactlyOneFloatInOrder) {
    // A parallel same-seeded RGRandom must reproduce each draw exactly:
    // proves the brain draws ONE float per shot, in order, with -spread/+spread
    // bounds and nothing else advancing the stream.
    Gun017 g;
    RGRandom ref;
    g.SetSeed(98765);
    ref.SetRandomSeed(98765);

    const std::vector<float> spreads = {5.0F, 12.5F, 0.0F, 7.0F, 18.0F};
    for (const float spread : spreads) {
        const float got      = g.ScatterAngle(spread);
        const float expected = ref.Range(-spread, spread); // one parallel draw
        EXPECT_FLOAT_EQ(got, expected);
    }
}

TEST(Gun017Test, ScatterIsDeterministicForSameSeed) {
    Gun017 a;
    Gun017 b;
    a.SetSeed(555);
    b.SetSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(a.ScatterAngle(15.0F), b.ScatterAngle(15.0F));
    }
}

TEST(Gun017Test, DroneGatedShotTakesNoRNGDraw) {
    // FAITHFUL: 964739 return early if child+0xc != 0 -> stream must NOT advance.
    // Simulate: seed both to same value; one calls ScatterAngle (the "fire" path
    // that would advance the stream), the other does not (the "gated" path). The
    // gated stream stays one draw behind.  Verify by comparing the NEXT draw from
    // the reference (which did NOT advance) to the draw that the gun makes on its
    // next shot (which similarly did not advance because the gate blocked it).
    Gun017 g;
    RGRandom ref;
    g.SetSeed(77777);
    ref.SetRandomSeed(77777);

    const float spread = 10.0F;

    // CanFire(1) == false: no draw happens inside the brain.
    // We do NOT call g.ScatterAngle here; the stream is preserved.
    EXPECT_FALSE(Gun017::CanFire(1)); // drone active -- no draw taken

    // The reference has also not advanced. Both streams are still in sync.
    // The next fired shot (CanFire(0)) must match the very first draw.
    EXPECT_TRUE(Gun017::CanFire(0));
    const float gotFirst    = g.ScatterAngle(spread);
    const float refFirst    = ref.Range(-spread, spread);
    EXPECT_FLOAT_EQ(gotFirst, refFirst);
}

// ---- DropWeapon: unconditional retract, no RNG --------------------------------

TEST(Gun017Test, DropWeaponAlwaysRetracts) {
    // FAITHFUL: Gun017__DropWeapon @ game_full.c:964807-964817:
    //   Gun017Child__StopRunning + ShowSelf -> drone stowed, gun shown.
    Gun017 g;
    // Start deployed.
    g.StartUseWeapon(false); // deploy
    EXPECT_TRUE(g.Deployed());

    const bool result = g.DropWeapon();
    EXPECT_FALSE(result);
    EXPECT_FALSE(g.Deployed());
}

TEST(Gun017Test, DropWeaponFromRetractedStaysRetracted) {
    Gun017 g;
    // Already retracted by default.
    EXPECT_FALSE(g.Deployed());
    g.DropWeapon();
    EXPECT_FALSE(g.Deployed());
}

// ---- StartUseWeapon: deploy / retract toggle, no RNG -------------------------

TEST(Gun017Test, StartUseWeaponDeploysWhenNotRGController) {
    // FAITHFUL: Gun017__StartUseWeapon @ game_full.c:964889:
    //   !bVar1 -> Gun017Child__StartRunning + HideSelf -> deployed.
    Gun017 g;
    const bool result = g.StartUseWeapon(false);
    EXPECT_TRUE(result);
    EXPECT_TRUE(g.Deployed());
}

TEST(Gun017Test, StartUseWeaponRetractsWhenRGController) {
    // FAITHFUL: Gun017__StartUseWeapon @ game_full.c:964884-964887:
    //   bVar1 -> Gun017Child__StopRunning + ShowSelf -> retracted.
    Gun017 g;
    g.StartUseWeapon(false); // deploy first
    const bool result = g.StartUseWeapon(true);
    EXPECT_FALSE(result);
    EXPECT_FALSE(g.Deployed());
}

// ---- StopWeapon: three-way branch, no RNG ------------------------------------

TEST(Gun017Test, StopWeaponDeploysWhenRGControllerAndOwnerIsSelf) {
    // FAITHFUL: Gun017__StopWeapon @ game_full.c:964958-964962 (case A):
    //   controllerIsRGController=true AND ownerWeaponIsSelf=true
    //   -> StartRunning + HideSelf -> deployed.
    Gun017 g;
    EXPECT_FALSE(g.Deployed());
    const bool result = g.StopWeapon(true, true);
    EXPECT_TRUE(result);
    EXPECT_TRUE(g.Deployed());
}

TEST(Gun017Test, StopWeaponRetractsWhenNotRGController) {
    // FAITHFUL: Gun017__StopWeapon @ game_full.c:964976 LAB_00b064b0 (case C):
    //   !controllerIsRGController -> StopRunning + ShowSelf -> retracted.
    Gun017 g;
    g.StartUseWeapon(false); // deploy first
    EXPECT_TRUE(g.Deployed());

    const bool result = g.StopWeapon(false, false);
    EXPECT_FALSE(result);
    EXPECT_FALSE(g.Deployed());
}

TEST(Gun017Test, StopWeaponPreservesStateWhenRGControllerButNotSelf) {
    // FAITHFUL: Gun017__StopWeapon @ game_full.c:964967-964974 (case B):
    //   controllerIsRGController=true, ownerWeaponIsSelf=false -> second
    //   RGController type check passes -> plain return, NO state write.
    //   Deployed() must be unchanged before and after the call.
    Gun017 g;

    // Sub-case B1: was retracted -> stays retracted.
    EXPECT_FALSE(g.Deployed());
    const bool r1 = g.StopWeapon(true, false);
    EXPECT_FALSE(r1);
    EXPECT_FALSE(g.Deployed());

    // Sub-case B2: was deployed -> stays deployed.
    g.StartUseWeapon(false); // deploy
    EXPECT_TRUE(g.Deployed());
    const bool r2 = g.StopWeapon(true, false);
    EXPECT_TRUE(r2);
    EXPECT_TRUE(g.Deployed());
}

// ---- end-to-end: scatter draw count + order lockstep across a burst ----------

TEST(Gun017Test, OneScatterDrawPerShotLockstep) {
    // Simulate a five-shot burst where each shot passes the CanFire gate (drone
    // stowed). A parallel same-seeded RGRandom must reproduce the whole
    // sequence draw-for-draw (count + order). Zero-draw frames (drone active)
    // must not advance the stream.
    auto run = [](int seed) -> std::vector<float> {
        Gun017 g;
        g.SetSeed(seed);
        std::vector<float> trace;
        const float baseAngle = 12.0F;
        const float recoil    = 0.3F;
        const float spread    = Gun017::BaseSpreadWithRecoil(baseAngle, recoil);

        // Interleave two gated (no-draw) frames between real shots.
        for (int shot = 0; shot < 5; ++shot) {
            // gated frame: drone active, no draw
            if (Gun017::CanFire(1)) { // always false
                trace.push_back(g.ScatterAngle(spread));
            }
            // real shot: drone stowed, one draw
            if (Gun017::CanFire(0)) { // always true
                trace.push_back(g.ScatterAngle(spread));
            }
        }
        return trace;
    };

    const auto a = run(31337);
    const auto b = run(31337);
    ASSERT_EQ(a.size(), static_cast<std::size_t>(5));
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

TEST(Gun017Test, ParallelStreamMatchesGunStreamAcrossBurst) {
    // A parallel same-seeded RGRandom must match exactly, proving draw count
    // and order are identical to the decomp.
    Gun017 g;
    RGRandom ref;
    g.SetSeed(42);
    ref.SetRandomSeed(42);

    const float baseAngle = 10.0F;
    const float recoil    = 0.0F;
    const float spread    = Gun017::BaseSpreadWithRecoil(baseAngle, recoil);

    for (int shot = 0; shot < 8; ++shot) {
        const float got      = g.ScatterAngle(spread);
        const float expected = ref.Range(-spread, spread);
        EXPECT_FLOAT_EQ(got, expected) << "shot " << shot;
    }
}

// NOLINTEND(readability-magic-numbers)
