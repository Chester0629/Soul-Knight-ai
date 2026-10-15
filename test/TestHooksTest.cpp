#include <gtest/gtest.h>

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

} // namespace
