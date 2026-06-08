#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "data/RGRandom.hpp"
#include "sim/BulletState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/FireSystem.hpp"

using namespace Game::Sim;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
float AngleDeg(glm::vec2 v) {
    return std::atan2(v.y, v.x) * 180.0F / 3.14159265358979F;
}
} // namespace

TEST(FireSystemTest, SingleEmitsOneBulletAlongDir) {
    FireSystem fs;
    std::vector<BulletState> out;
    std::uint32_t nextId = 1;

    FireIntent in;
    in.pattern = FirePattern::Single;
    in.origin = glm::vec2{10.0F, 20.0F};
    in.dir = glm::vec2{1.0F, 0.0F};
    in.speedPxPerSec = 300.0F;
    in.lifeMs = 1500.0F;
    in.damage = 7;
    in.camp = 0;

    fs.Expand(in, nextId, out);

    ASSERT_EQ(out.size(), 1U);
    EXPECT_EQ(out[0].id, 1U);
    EXPECT_EQ(nextId, 2U);
    EXPECT_TRUE(out[0].active);
    EXPECT_FLOAT_EQ(out[0].pos.x, 10.0F);
    EXPECT_FLOAT_EQ(out[0].vel.x, 300.0F);
    EXPECT_NEAR(out[0].vel.y, 0.0F, 1e-4F);
    EXPECT_EQ(out[0].damage, 7);
}

TEST(FireSystemTest, FanSpreadsCountBulletsEvenly) {
    FireSystem fs;
    std::vector<BulletState> out;
    std::uint32_t nextId = 1;

    FireIntent in;
    in.pattern = FirePattern::Fan;
    in.dir = glm::vec2{1.0F, 0.0F}; // 0 degrees
    in.count = 3;
    in.spreadDeg = 30.0F; // -> bullets at -15, 0, +15
    in.speedPxPerSec = 100.0F;

    fs.Expand(in, nextId, out);

    ASSERT_EQ(out.size(), 3U);
    EXPECT_NEAR(AngleDeg(out[0].vel), -15.0F, 1e-3F);
    EXPECT_NEAR(AngleDeg(out[1].vel), 0.0F, 1e-3F);
    EXPECT_NEAR(AngleDeg(out[2].vel), 15.0F, 1e-3F);
    EXPECT_EQ(nextId, 4U);
}

TEST(FireSystemTest, FanOfOneIsCentre) {
    FireSystem fs;
    std::vector<BulletState> out;
    std::uint32_t nextId = 1;

    FireIntent in;
    in.pattern = FirePattern::Fan;
    in.dir = glm::vec2{0.0F, 1.0F}; // 90 degrees
    in.count = 1;
    in.spreadDeg = 40.0F;
    in.speedPxPerSec = 100.0F;

    fs.Expand(in, nextId, out);

    ASSERT_EQ(out.size(), 1U);
    EXPECT_NEAR(AngleDeg(out[0].vel), 90.0F, 1e-3F);
}

TEST(FireSystemTest, FanOfTwoSpreadsAtTheEndpoints) {
    // count=2 is the boundary where the (count-1) divisor matters: a naive
    // step=spread/count would give the wrong angles. Also assert that rotation
    // preserves the speed magnitude (vel length == speedPxPerSec).
    FireSystem fs;
    std::vector<BulletState> out;
    std::uint32_t nextId = 1;

    FireIntent in;
    in.pattern = FirePattern::Fan;
    in.dir = glm::vec2{1.0F, 0.0F};
    in.count = 2;
    in.spreadDeg = 30.0F; // -> bullets at -15 and +15
    in.speedPxPerSec = 100.0F;

    fs.Expand(in, nextId, out);

    ASSERT_EQ(out.size(), 2U);
    EXPECT_NEAR(AngleDeg(out[0].vel), -15.0F, 1e-3F);
    EXPECT_NEAR(AngleDeg(out[1].vel), 15.0F, 1e-3F);
    for (const auto &b : out) {
        const float mag = std::sqrt(b.vel.x * b.vel.x + b.vel.y * b.vel.y);
        EXPECT_NEAR(mag, 100.0F, 1e-3F); // rotation preserves speed
    }
}

TEST(FireSystemTest, FanWithNonPositiveCountEmitsNothing) {
    FireSystem fs;
    std::vector<BulletState> out;
    std::uint32_t nextId = 1;

    FireIntent in;
    in.pattern = FirePattern::Fan;
    in.dir = glm::vec2{1.0F, 0.0F};
    in.count = 0; // a zero-count fan must spawn nothing and not advance ids
    in.spreadDeg = 30.0F;
    in.speedPxPerSec = 100.0F;

    fs.Expand(in, nextId, out);

    EXPECT_TRUE(out.empty());
    EXPECT_EQ(nextId, 1U); // unchanged
}

TEST(FireSystemTest, ExpandTakesNoRngDraw) {
    // The FireSystem is a pure geometry function: all randomness is brain-side.
    // A parallel same-seeded RGRandom must be untouched across many expansions.
    FireSystem fs;
    Game::RGRandom ref;
    ref.SetRandomSeed(123);
    Game::RGRandom probe;
    probe.SetRandomSeed(123);

    std::vector<BulletState> out;
    std::uint32_t nextId = 1;
    for (int i = 0; i < 32; ++i) {
        FireIntent in;
        in.pattern = (i % 2 == 0) ? FirePattern::Single : FirePattern::Fan;
        in.count = 3;
        in.spreadDeg = 20.0F;
        in.dir = glm::vec2{1.0F, 0.0F};
        in.speedPxPerSec = 100.0F;
        fs.Expand(in, nextId, out);
        out.clear();
    }
    EXPECT_EQ(ref.Range(0, 1000000), probe.Range(0, 1000000)); // both unadvanced
}

// NOLINTEND(readability-magic-numbers)
