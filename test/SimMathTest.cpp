#include <gtest/gtest.h>

#include <cmath>

#include "sim/SimMath.hpp"

using Game::Sim::Normalize;
using Game::Sim::RotateDeg;

// NOLINTBEGIN(readability-magic-numbers)

TEST(SimMathTest, NormalizeUnitAndZero) {
    const glm::vec2 n = Normalize(glm::vec2{3.0F, 4.0F});
    EXPECT_NEAR(std::sqrt(n.x * n.x + n.y * n.y), 1.0F, 1e-5F);
    EXPECT_NEAR(n.x, 0.6F, 1e-5F);
    EXPECT_NEAR(n.y, 0.8F, 1e-5F);
    const glm::vec2 z = Normalize(glm::vec2{0.0F, 0.0F});
    EXPECT_FLOAT_EQ(z.x, 1.0F);
    EXPECT_FLOAT_EQ(z.y, 0.0F);
}

TEST(SimMathTest, RotateDegCcw) {
    const glm::vec2 r = RotateDeg(glm::vec2{1.0F, 0.0F}, 90.0F);
    EXPECT_NEAR(r.x, 0.0F, 1e-5F);
    EXPECT_NEAR(r.y, 1.0F, 1e-5F);
    const glm::vec2 z = RotateDeg(glm::vec2{1.0F, 0.0F}, 0.0F);
    EXPECT_FLOAT_EQ(z.x, 1.0F);
    EXPECT_FLOAT_EQ(z.y, 0.0F);
}

TEST(SimMathTest, CirclesOverlapByCenterDistance) {
    using Game::Sim::CirclesOverlap;
    // centers 5 apart, radii 3+3=6 -> overlap; radii 2+2=4 -> no overlap.
    EXPECT_TRUE(CirclesOverlap(glm::vec2{0.0F, 0.0F}, 3.0F, glm::vec2{5.0F, 0.0F}, 3.0F));
    EXPECT_FALSE(CirclesOverlap(glm::vec2{0.0F, 0.0F}, 2.0F, glm::vec2{5.0F, 0.0F}, 2.0F));
    // exact touch (distance == sum) counts as overlap (inclusive).
    EXPECT_TRUE(CirclesOverlap(glm::vec2{0.0F, 0.0F}, 2.0F, glm::vec2{4.0F, 0.0F}, 2.0F));
}

// NOLINTEND(readability-magic-numbers)
