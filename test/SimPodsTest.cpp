#include <gtest/gtest.h>

#include "sim/BulletState.hpp"
#include "sim/EntityState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/SimEvent.hpp"
#include "sim/WorldCollision.hpp"

using namespace Game::Sim;

// NOLINTBEGIN(readability-magic-numbers)

TEST(SimPodsTest, DefaultsAreInert) {
    BulletState b;
    EXPECT_FALSE(b.active);
    EXPECT_EQ(b.camp, 0);

    EntityState e;
    EXPECT_FALSE(e.awake);
    EXPECT_FALSE(e.dead);
    EXPECT_EQ(e.roomId, -1);

    FireIntent f;
    EXPECT_EQ(f.pattern, FirePattern::Single);
    EXPECT_EQ(f.count, 1);

    SimEvent ev;
    EXPECT_EQ(ev.type, SimEventType::AnimTrigger);
}

TEST(SimPodsTest, NullWorldCollisionNeverBlocks) {
    NullWorldCollision w;
    EXPECT_FALSE(w.Blocks(glm::vec2{0.0F, 0.0F}, 16.0F));
    EXPECT_FALSE(w.Blocks(glm::vec2{1000.0F, -1000.0F}, 64.0F));
}

// NOLINTEND(readability-magic-numbers)
