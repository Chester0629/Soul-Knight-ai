#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "game/TestHooks.hpp"

namespace {
using Game::ParseRoomTeleport;

TEST(RoomTeleportHookTest, ValidTwoFieldForm) {
    const auto r = ParseRoomTeleport("60,1");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.frame, 60);
    EXPECT_EQ(r.room, 1);
    EXPECT_EQ(r.dx, 0.0F);
    EXPECT_EQ(r.dy, 0.0F);
    EXPECT_TRUE(ParseRoomTeleport("12,3").ok);
}

TEST(RoomTeleportHookTest, ValidFourFieldForm) {
    auto r = ParseRoomTeleport("12,4,0,-160");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.frame, 12);
    EXPECT_EQ(r.room, 4);
    EXPECT_EQ(r.dx, 0.0F);
    EXPECT_EQ(r.dy, -160.0F);
    r = ParseRoomTeleport("12,3,-12.5,7.25");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.dx, -12.5F);
    EXPECT_EQ(r.dy, 7.25F);
}

TEST(RoomTeleportHookTest, RejectsWrongFieldCounts) {
    EXPECT_FALSE(ParseRoomTeleport("").ok);
    EXPECT_FALSE(ParseRoomTeleport("12").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,0").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,0,5,6").ok);
}

TEST(RoomTeleportHookTest, RejectsEmptyFieldsAndTrailingGarbage) {
    EXPECT_FALSE(ParseRoomTeleport("12,").ok);
    EXPECT_FALSE(ParseRoomTeleport(",4").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,,5").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,0,").ok);
    EXPECT_FALSE(ParseRoomTeleport("12x,4").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4x").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,0,5px").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4.5").ok);
}

TEST(RoomTeleportHookTest, RejectsNonFiniteOffsets) {
    EXPECT_FALSE(ParseRoomTeleport("12,4,nan,0").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,0,inf").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,-inf,0").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,1e999,0").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,0,-1e999").ok);
}

TEST(RoomTeleportHookTest, OffsetMagnitudeBound) {
    EXPECT_TRUE(ParseRoomTeleport("12,4,4096,-4096").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,4096.5,0").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,4,0,-4096.5").ok);
}

TEST(RoomTeleportHookTest, RejectedSpecCarriesReasonAndNoOffset) {
    const auto r = ParseRoomTeleport("12,4,nan,0");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
    EXPECT_EQ(r.dx, 0.0F);
    EXPECT_EQ(r.dy, 0.0F);
}

TEST(RoomTeleportHookTest, RejectsIntegerOverflow) {
    EXPECT_FALSE(ParseRoomTeleport("99999999999999999999,4").ok);
    EXPECT_FALSE(ParseRoomTeleport("12,99999999999").ok);
}

TEST(RoomTeleportHookTest, ValidDxInvalidDyZeroesBothOffsets) {
    const auto r = ParseRoomTeleport("12,4,5,nan");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.dx, 0.0F);
    EXPECT_EQ(r.dy, 0.0F);
}

// --- P2.18 evidence hooks: SK_FORCE_DASH / SK_WALK_DIR / SK_DT_SPIKE / SK_TRACE_POS --------------
TEST(ForceDashHookTest, ParsesStrictly) {
    auto r = Game::ParseForceDash("10,1,0");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.frame, 10);
    EXPECT_EQ(r.dx, 1.0F);
    EXPECT_EQ(r.dy, 0.0F);
    r = Game::ParseForceDash("0,-0.5,2.5");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.dx, -0.5F);
    EXPECT_FALSE(Game::ParseForceDash("10,1").ok);
    EXPECT_FALSE(Game::ParseForceDash("10,1,0,0").ok);
    EXPECT_FALSE(Game::ParseForceDash("-1,1,0").ok) << "negative frame";
    EXPECT_FALSE(Game::ParseForceDash("10,0,0").ok) << "zero direction";
    EXPECT_FALSE(Game::ParseForceDash("10,nan,1").ok);
    EXPECT_FALSE(Game::ParseForceDash("10,1,inf").ok);
    EXPECT_FALSE(Game::ParseForceDash("10,1,9999999").ok);
    EXPECT_FALSE(Game::ParseForceDash("x,1,0").ok);
    const auto bad = Game::ParseForceDash("10,1,junk");
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.dx, 0.0F);
    EXPECT_EQ(bad.dy, 0.0F);
}

TEST(WalkDirHookTest, ParsesStrictly) {
    auto r = Game::ParseWalkDir("1,0");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.dx, 1.0F);
    r = Game::ParseWalkDir("-3,4");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.dy, 4.0F);
    EXPECT_FALSE(Game::ParseWalkDir("").ok);
    EXPECT_FALSE(Game::ParseWalkDir("1").ok);
    EXPECT_FALSE(Game::ParseWalkDir("1,2,3").ok);
    EXPECT_FALSE(Game::ParseWalkDir("0,0").ok);
    EXPECT_FALSE(Game::ParseWalkDir("1,nan").ok);
    EXPECT_FALSE(Game::ParseWalkDir("1,2x").ok);
}

TEST(ForceFireHookTest, ParsesStrictly) {
    auto r = Game::ParseForceFire("60,0,1");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.frame, 60);
    EXPECT_EQ(r.dy, 1.0F);
    EXPECT_GT(r.count, 1000000L); // default: until the run ends
    r = Game::ParseForceFire("60,-1,0,30");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.dx, -1.0F);
    EXPECT_EQ(r.count, 30);
    EXPECT_FALSE(Game::ParseForceFire("").ok);
    EXPECT_FALSE(Game::ParseForceFire("60,1").ok);
    EXPECT_FALSE(Game::ParseForceFire("60,1,0,5,6").ok);
    EXPECT_FALSE(Game::ParseForceFire("60,0,0").ok);
    EXPECT_FALSE(Game::ParseForceFire("-1,1,0").ok);
    EXPECT_FALSE(Game::ParseForceFire("60,nan,0").ok);
    EXPECT_FALSE(Game::ParseForceFire("60,1,0,0").ok);
    EXPECT_FALSE(Game::ParseForceFire("60,1,0,x").ok);
    // R42: a tiny / huge component must not underflow / overflow the normalisation.
    for (const char *txt : {"60,1e-30,0", "60,0,-1e-30", "60,1e-30,1e-30", "60,3e-39,0", "60,4096,4096"}) {
        const auto t = Game::ParseForceFire(txt);
        if (!t.ok) {
            continue; // a parser-level rejection (e.g. a float-denormal ERANGE) is also acceptable
        }
        const double len = std::sqrt(static_cast<double>(t.ux) * t.ux + static_cast<double>(t.uy) * t.uy);
        EXPECT_NEAR(len, 1.0, 1e-6) << txt;
        EXPECT_TRUE(std::isfinite(t.ux) && std::isfinite(t.uy)) << txt;
    }
    {
        const auto t = Game::ParseForceFire("100,1e-30,0");
        ASSERT_TRUE(t.ok);
        EXPECT_FLOAT_EQ(t.ux, 1.0F);
        EXPECT_FLOAT_EQ(t.uy, 0.0F);
        const auto d = Game::ParseForceFire("100,1e-30,1e-30");
        ASSERT_TRUE(d.ok);
        EXPECT_NEAR(d.ux, 0.70710678F, 1e-6F);
        EXPECT_NEAR(d.uy, 0.70710678F, 1e-6F);
    }
    EXPECT_FALSE(Game::ParseForceFire("60,1e30,0").ok); // beyond |v| <= 4096
    EXPECT_FALSE(Game::ParseForceFire("60,1e30,1e30").ok);
    // the normaliser itself (double, any magnitude)
    float ux = 0.0F;
    float uy = 0.0F;
    EXPECT_TRUE(Game::NormalizeHookDir(1e30, 1e30, ux, uy));
    EXPECT_NEAR(ux, 0.70710678F, 1e-6F);
    EXPECT_TRUE(Game::NormalizeHookDir(5e-324, 0.0, ux, uy)); // a double denormal
    EXPECT_FLOAT_EQ(ux, 1.0F);
    EXPECT_FALSE(Game::NormalizeHookDir(0.0, 0.0, ux, uy));
    EXPECT_FLOAT_EQ(ux, 0.0F);
    EXPECT_FALSE(Game::NormalizeHookDir(std::nan(""), 1.0, ux, uy));
    EXPECT_FALSE(Game::NormalizeHookDir(INFINITY, 0.0, ux, uy));
    const auto bad = Game::ParseForceFire("60,1,junk");
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.dx, 0.0F);
}

// SLICE 4: SK_FORCE_AIM shares SK_FORCE_FIRE's strict grammar (aim only, never fires).
TEST(ForceAimHookTest, ParsesStrictlyLikeForceFire) {
    auto r = Game::ParseForceAim("40,-1,0");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.frame, 40);
    EXPECT_FLOAT_EQ(r.ux, -1.0F);
    EXPECT_FLOAT_EQ(r.uy, 0.0F);
    EXPECT_GT(r.count, 1000000L);
    r = Game::ParseForceAim("40,1,1,25");
    ASSERT_TRUE(r.ok);
    EXPECT_NEAR(r.ux, 0.70710678F, 1e-6F);
    EXPECT_EQ(r.count, 25);
    // every malformed form is rejected whole, and a rejected spec never carries a direction
    for (const char *bad : {"", "40", "40,1", "40,0,0", "-1,1,0", "40,nan,1", "40,1,inf", "40,1e30,0",
                            "40,1,0,0", "40,1,0,x", "40,1,0,5,6", "40.5,1,0", "40,1,0 ", "4x,1,0"}) {
        const auto t = Game::ParseForceAim(bad);
        EXPECT_FALSE(t.ok) << bad;
        EXPECT_EQ(t.dx, 0.0F) << bad;
        EXPECT_EQ(t.ux, 0.0F) << bad;
    }
    // a tiny component does not underflow the normalisation (R42 for the shared parser)
    const auto tiny = Game::ParseForceAim("40,-1e-30,0");
    ASSERT_TRUE(tiny.ok);
    EXPECT_FLOAT_EQ(tiny.ux, -1.0F);
}

TEST(DtSpikeHookTest, ParsesStrictly) {
    auto r = Game::ParseDtSpike("5,5000");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.frame, 5);
    EXPECT_EQ(r.ms, 5000.0F);
    EXPECT_TRUE(Game::ParseDtSpike("0,3600000").ok);
    EXPECT_FALSE(Game::ParseDtSpike("0,3600001").ok) << "above the 1 hour cap";
    EXPECT_FALSE(Game::ParseDtSpike("5,0").ok);
    EXPECT_FALSE(Game::ParseDtSpike("5,-3").ok);
    EXPECT_FALSE(Game::ParseDtSpike("5").ok);
    EXPECT_FALSE(Game::ParseDtSpike("-1,100").ok);
    EXPECT_FALSE(Game::ParseDtSpike("5,nan").ok);
    EXPECT_FALSE(Game::ParseDtSpike("5,1e999").ok);
    const auto bad = Game::ParseDtSpike("5,zzz");
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.ms, 0.0F);
}

TEST(TraceFramesHookTest, ParsesStrictly) {
    auto r = Game::ParseFrameList("3,250");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.frames, (std::vector<long>{3, 250}));
    EXPECT_TRUE(Game::ParseFrameList("7").ok);
    EXPECT_FALSE(Game::ParseFrameList("").ok);
    EXPECT_FALSE(Game::ParseFrameList("3,").ok);
    EXPECT_FALSE(Game::ParseFrameList("3,-1").ok);
    EXPECT_FALSE(Game::ParseFrameList("3,x").ok);
    const auto bad = Game::ParseFrameList("3,x");
    EXPECT_TRUE(bad.frames.empty()) << "a rejected list carries no frames";
    std::string many;
    for (int i = 0; i < 33; ++i) {
        many += (i ? "," : "") + std::to_string(i);
    }
    EXPECT_FALSE(Game::ParseFrameList(many).ok) << "more than 32 frames";
}

TEST(StingStandHookTest, ParsesStrictly) {
    const auto ok = Game::ParseStingStand("60,1");
    ASSERT_TRUE(ok.ok);
    EXPECT_EQ(ok.frame, 60);
    EXPECT_EQ(ok.room, 1);
    EXPECT_TRUE(Game::ParseStingStand("0,0").ok);
    EXPECT_FALSE(Game::ParseStingStand("").ok);
    EXPECT_FALSE(Game::ParseStingStand("60").ok);
    EXPECT_FALSE(Game::ParseStingStand("60,1,2").ok) << "no offset form";
    EXPECT_FALSE(Game::ParseStingStand("60,1,").ok);
    EXPECT_FALSE(Game::ParseStingStand("-1,1").ok);
    EXPECT_FALSE(Game::ParseStingStand("60,-1").ok);
    EXPECT_FALSE(Game::ParseStingStand("60,x").ok);
    EXPECT_FALSE(Game::ParseStingStand("6x,1").ok);
    EXPECT_FALSE(Game::ParseStingStand("60,99999999999").ok) << "room overflow";
    const auto bad = Game::ParseStingStand("60,x");
    EXPECT_EQ(bad.frame, -1) << "a rejected spec carries no frame";
}

TEST(ChestStandHookTest, ParsesStrictly) {
    const auto ok = Game::ParseChestStand("60,64");
    ASSERT_TRUE(ok.ok);
    EXPECT_EQ(ok.frame, 60);
    EXPECT_FLOAT_EQ(ok.below, 64.0F);
    EXPECT_TRUE(Game::ParseChestStand("0,0").ok);
    EXPECT_TRUE(Game::ParseChestStand("5,4096").ok);
    EXPECT_FALSE(Game::ParseChestStand("5,4097").ok);
    EXPECT_FALSE(Game::ParseChestStand("").ok);
    EXPECT_FALSE(Game::ParseChestStand("60").ok);
    EXPECT_FALSE(Game::ParseChestStand("60,1,2").ok);
    EXPECT_FALSE(Game::ParseChestStand("-1,10").ok);
    EXPECT_FALSE(Game::ParseChestStand("60,-1").ok);
    EXPECT_FALSE(Game::ParseChestStand("60,nan").ok);
    EXPECT_FALSE(Game::ParseChestStand("60,inf").ok);
    EXPECT_FALSE(Game::ParseChestStand("60,1x").ok);
    const auto bad = Game::ParseChestStand("60,nan");
    EXPECT_EQ(bad.frame, -1);
    EXPECT_FLOAT_EQ(bad.below, 0.0F);
}

} // namespace
