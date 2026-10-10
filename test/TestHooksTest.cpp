#include <gtest/gtest.h>

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

} // namespace
