#include <algorithm>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Physics/CollisionWorld.hpp"
#include "Util/Collider.hpp"

using Physics::CollisionWorld;
using Util::Collider;

// NOLINTBEGIN(readability-magic-numbers)

// --------------------------------------------------------------------------
// Narrowphase: AABB vs AABB
// --------------------------------------------------------------------------

TEST(ColliderTest, AABBOverlapTrue) {
    const Collider a = Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f});
    const Collider b = Collider::MakeAABB({5.0f, 5.0f}, {10.0f, 10.0f});
    EXPECT_TRUE(Util::Overlap(a, b));
    EXPECT_TRUE(Util::Overlap(b, a));
}

TEST(ColliderTest, AABBOverlapTouchingIsTrue) {
    // a spans x in [-5, 5]; b spans x in [5, 15] -> edges touch at x == 5.
    const Collider a = Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f});
    const Collider b = Collider::MakeAABB({10.0f, 0.0f}, {10.0f, 10.0f});
    EXPECT_TRUE(Util::Overlap(a, b));
}

TEST(ColliderTest, AABBSeparatedOnXIsFalse) {
    const Collider a = Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f});
    const Collider b = Collider::MakeAABB({100.0f, 0.0f}, {10.0f, 10.0f});
    EXPECT_FALSE(Util::Overlap(a, b));
}

TEST(ColliderTest, AABBSeparatedOnYIsFalse) {
    const Collider a = Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f});
    const Collider b = Collider::MakeAABB({0.0f, 100.0f}, {10.0f, 10.0f});
    EXPECT_FALSE(Util::Overlap(a, b));
}

// --------------------------------------------------------------------------
// Narrowphase: Circle vs Circle
// --------------------------------------------------------------------------

TEST(ColliderTest, CircleOverlapTrue) {
    // Centers 5 apart, radii 3 + 3 = 6 > 5 -> overlap.
    const Collider a = Collider::MakeCircle({0.0f, 0.0f}, 3.0f);
    const Collider b = Collider::MakeCircle({5.0f, 0.0f}, 3.0f);
    EXPECT_TRUE(Util::Overlap(a, b));
    EXPECT_TRUE(Util::Overlap(b, a));
}

TEST(ColliderTest, CircleOverlapFalse) {
    // Centers 10 apart, radii 3 + 3 = 6 < 10 -> no overlap.
    const Collider a = Collider::MakeCircle({0.0f, 0.0f}, 3.0f);
    const Collider b = Collider::MakeCircle({10.0f, 0.0f}, 3.0f);
    EXPECT_FALSE(Util::Overlap(a, b));
}

TEST(ColliderTest, CircleTouchingIsTrue) {
    // Centers 6 apart, radii sum exactly 6 -> touching counts as overlap.
    const Collider a = Collider::MakeCircle({0.0f, 0.0f}, 3.0f);
    const Collider b = Collider::MakeCircle({6.0f, 0.0f}, 3.0f);
    EXPECT_TRUE(Util::Overlap(a, b));
}

// --------------------------------------------------------------------------
// Narrowphase: AABB vs Circle (both argument orders)
// --------------------------------------------------------------------------

TEST(ColliderTest, AABBCircleEdgeOverlap) {
    // Box spans x in [-5, 5]; circle center at x = 7, radius 3 reaches x = 4.
    const Collider box = Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f});
    const Collider circle = Collider::MakeCircle({7.0f, 0.0f}, 3.0f);
    EXPECT_TRUE(Util::Overlap(box, circle));
    EXPECT_EQ(Util::Overlap(box, circle), Util::Overlap(circle, box));
}

TEST(ColliderTest, AABBCircleCornerOverlap) {
    // Box corner at (5, 5); circle center just outside the corner.
    const Collider box = Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f});
    const Collider nearCircle = Collider::MakeCircle({6.0f, 6.0f}, 2.0f);
    EXPECT_TRUE(Util::Overlap(box, nearCircle));
    EXPECT_EQ(Util::Overlap(box, nearCircle), Util::Overlap(nearCircle, box));

    // Past the corner diagonally: distance to corner (5,5) is sqrt(2)*3 ~= 4.24
    // which exceeds radius 2 -> no overlap.
    const Collider farCircle = Collider::MakeCircle({8.0f, 8.0f}, 2.0f);
    EXPECT_FALSE(Util::Overlap(box, farCircle));
    EXPECT_EQ(Util::Overlap(box, farCircle), Util::Overlap(farCircle, box));
}

// --------------------------------------------------------------------------
// MTV resolution
// --------------------------------------------------------------------------

TEST(ColliderTest, ResolveMTVZeroWhenSeparated) {
    const Collider a = Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f});
    const Collider b = Collider::MakeAABB({100.0f, 0.0f}, {10.0f, 10.0f});
    const glm::vec2 mtv = Util::ResolveMTV(a, b);
    EXPECT_FLOAT_EQ(mtv.x, 0.0f);
    EXPECT_FLOAT_EQ(mtv.y, 0.0f);
}

TEST(ColliderTest, ResolveMTVSeparatesAABBs) {
    // a center 0, b center (8,0); both half-size 5 -> 2 px penetration on x.
    const Collider a = Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f});
    const Collider b = Collider::MakeAABB({8.0f, 0.0f}, {10.0f, 10.0f});
    const glm::vec2 mtv = Util::ResolveMTV(a, b);
    EXPECT_FLOAT_EQ(mtv.x, -2.0f);
    EXPECT_FLOAT_EQ(mtv.y, 0.0f);

    // Applying the MTV should clear the overlap (edges just touch).
    Collider moved = a;
    moved.center += mtv;
    EXPECT_TRUE(Util::Overlap(moved, b)); // touching boundary remains true
    const glm::vec2 mtv2 = Util::ResolveMTV(moved, b);
    EXPECT_NEAR(mtv2.x, 0.0f, 1e-4f);
}

// --------------------------------------------------------------------------
// CollisionWorld::Query
// --------------------------------------------------------------------------

TEST(CollisionWorldTest, QueryReturnsOverlappingExcludesFar) {
    CollisionWorld world(64.0f);
    const auto hNear =
        world.Add(Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f}), 1);
    const auto hAlsoNear =
        world.Add(Collider::MakeAABB({8.0f, 0.0f}, {10.0f, 10.0f}), 2);
    const auto hFar =
        world.Add(Collider::MakeAABB({1000.0f, 1000.0f}, {10.0f, 10.0f}), 3);

    const Collider area = Collider::MakeAABB({0.0f, 0.0f}, {12.0f, 12.0f});
    const std::vector<CollisionWorld::Handle> hits = world.Query(area);

    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0], hNear);
    EXPECT_EQ(hits[1], hAlsoNear);
    EXPECT_TRUE(std::find(hits.begin(), hits.end(), hFar) == hits.end());
}

TEST(CollisionWorldTest, QueryWithCircleArea) {
    CollisionWorld world(64.0f);
    const auto h1 = world.Add(Collider::MakeCircle({0.0f, 0.0f}, 5.0f), 10);
    world.Add(Collider::MakeCircle({500.0f, 0.0f}, 5.0f), 20);

    const Collider area = Collider::MakeCircle({3.0f, 0.0f}, 5.0f);
    const std::vector<CollisionWorld::Handle> hits = world.Query(area);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], h1);
}

// --------------------------------------------------------------------------
// CollisionWorld::ComputePairs
// --------------------------------------------------------------------------

TEST(CollisionWorldTest, ComputePairsFindsOverlapExcludesDistant) {
    CollisionWorld world(64.0f);
    const auto a = world.Add(Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f}));
    const auto b = world.Add(Collider::MakeAABB({6.0f, 0.0f}, {10.0f, 10.0f}));
    world.Add(Collider::MakeAABB({1000.0f, 1000.0f}, {10.0f, 10.0f}));

    const std::vector<std::pair<CollisionWorld::Handle, CollisionWorld::Handle>>
        pairs = world.ComputePairs();

    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_LT(pairs[0].first, pairs[0].second);
    EXPECT_EQ(pairs[0].first, a);
    EXPECT_EQ(pairs[0].second, b);
}

TEST(CollisionWorldTest, ComputePairsSortedAndDeduped) {
    CollisionWorld world(16.0f); // small cells so colliders span many cells
    // Three mutually overlapping big boxes -> three pairs, each reported once.
    const auto a = world.Add(Collider::MakeAABB({0.0f, 0.0f}, {40.0f, 40.0f}));
    const auto b = world.Add(Collider::MakeAABB({5.0f, 0.0f}, {40.0f, 40.0f}));
    const auto c = world.Add(Collider::MakeAABB({0.0f, 5.0f}, {40.0f, 40.0f}));

    const auto pairs = world.ComputePairs();
    ASSERT_EQ(pairs.size(), 3u);

    // Sorted ascending and every pair has first < second.
    for (const auto &p : pairs) {
        EXPECT_LT(p.first, p.second);
    }
    EXPECT_TRUE(std::is_sorted(pairs.begin(), pairs.end()));

    std::vector<std::pair<CollisionWorld::Handle, CollisionWorld::Handle>>
        expected = {{a, b}, {a, c}, {b, c}};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(pairs, expected);
}

// --------------------------------------------------------------------------
// CollisionWorld::Raycast
// --------------------------------------------------------------------------

TEST(CollisionWorldTest, RaycastHitsAABBInPath) {
    CollisionWorld world(64.0f);
    const auto target =
        world.Add(Collider::MakeAABB({50.0f, 0.0f}, {10.0f, 10.0f}), 7);

    // Ray from origin pointing +x, unit direction. Box near face at x = 45.
    const CollisionWorld::RayHit hit =
        world.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 100.0f);

    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.handle, target);
    EXPECT_NEAR(hit.t, 45.0f, 1e-4f);
    EXPECT_NEAR(hit.point.x, 45.0f, 1e-4f);
    EXPECT_NEAR(hit.point.y, 0.0f, 1e-4f);
}

TEST(CollisionWorldTest, RaycastMissesOffToTheSide) {
    CollisionWorld world(64.0f);
    // Box centered well above the ray's path on y.
    world.Add(Collider::MakeAABB({50.0f, 100.0f}, {10.0f, 10.0f}), 7);

    const CollisionWorld::RayHit hit =
        world.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 200.0f);
    EXPECT_FALSE(hit.hit);
}

TEST(CollisionWorldTest, RaycastRespectsMaxDist) {
    CollisionWorld world(64.0f);
    world.Add(Collider::MakeAABB({50.0f, 0.0f}, {10.0f, 10.0f}), 7);

    // Near face is at x = 45; maxDist of 40 should not reach it.
    const CollisionWorld::RayHit miss =
        world.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 40.0f);
    EXPECT_FALSE(miss.hit);

    // maxDist comfortably past the near face hits.
    const CollisionWorld::RayHit hit =
        world.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 50.0f);
    EXPECT_TRUE(hit.hit);
}

TEST(CollisionWorldTest, RaycastNearestOfMany) {
    CollisionWorld world(64.0f);
    const auto farBox =
        world.Add(Collider::MakeAABB({200.0f, 0.0f}, {10.0f, 10.0f}), 1);
    const auto nearBox =
        world.Add(Collider::MakeAABB({60.0f, 0.0f}, {10.0f, 10.0f}), 2);

    const CollisionWorld::RayHit hit =
        world.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 500.0f);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.handle, nearBox);
    EXPECT_NE(hit.handle, farBox);
}

TEST(CollisionWorldTest, RaycastHitsCircle) {
    CollisionWorld world(64.0f);
    const auto target = world.Add(Collider::MakeCircle({30.0f, 0.0f}, 5.0f), 9);

    const CollisionWorld::RayHit hit =
        world.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 100.0f);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.handle, target);
    EXPECT_NEAR(hit.t, 25.0f, 1e-4f); // enters circle at x = 30 - 5 = 25
}

// --------------------------------------------------------------------------
// Lifecycle: Update / Remove / GetUserData
// --------------------------------------------------------------------------

TEST(CollisionWorldTest, UpdateMovesColliderInGrid) {
    CollisionWorld world(64.0f);
    const auto h = world.Add(Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f}), 5);

    const Collider area = Collider::MakeAABB({500.0f, 0.0f}, {12.0f, 12.0f});
    EXPECT_TRUE(world.Query(area).empty());

    world.Update(h, Collider::MakeAABB({500.0f, 0.0f}, {10.0f, 10.0f}));
    const std::vector<CollisionWorld::Handle> hits = world.Query(area);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], h);
    EXPECT_EQ(world.GetUserData(h), 5u); // user data preserved across Update
}

TEST(CollisionWorldTest, RemoveDropsCollider) {
    CollisionWorld world(64.0f);
    const auto h = world.Add(Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f}), 5);
    EXPECT_EQ(world.Size(), 1u);

    world.Remove(h);
    EXPECT_EQ(world.Size(), 0u);
    EXPECT_EQ(world.GetUserData(h), 0u);

    const Collider area = Collider::MakeAABB({0.0f, 0.0f}, {12.0f, 12.0f});
    EXPECT_TRUE(world.Query(area).empty());
    EXPECT_TRUE(world.ComputePairs().empty());
}

TEST(CollisionWorldTest, HandlesAreStableAfterRemove) {
    CollisionWorld world(64.0f);
    const auto h1 = world.Add(Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f}));
    world.Remove(h1);
    const auto h2 = world.Add(Collider::MakeAABB({0.0f, 0.0f}, {10.0f, 10.0f}));
    EXPECT_NE(h1, h2); // retired handles are not reused
}

// --------------------------------------------------------------------------
// Determinism
// --------------------------------------------------------------------------

TEST(CollisionWorldTest, DeterministicOutputs) {
    auto build = []() {
        CollisionWorld world(32.0f);
        world.Add(Collider::MakeAABB({0.0f, 0.0f}, {20.0f, 20.0f}));
        world.Add(Collider::MakeAABB({10.0f, 0.0f}, {20.0f, 20.0f}));
        world.Add(Collider::MakeAABB({0.0f, 10.0f}, {20.0f, 20.0f}));
        world.Add(Collider::MakeCircle({5.0f, 5.0f}, 8.0f));
        return world;
    };

    const CollisionWorld a = build();
    const CollisionWorld b = build();

    const Collider area = Collider::MakeAABB({5.0f, 5.0f}, {40.0f, 40.0f});
    EXPECT_EQ(a.Query(area), b.Query(area));
    EXPECT_EQ(a.ComputePairs(), b.ComputePairs());

    const CollisionWorld::RayHit ha =
        a.Raycast({-100.0f, 0.0f}, {1.0f, 0.0f}, 1000.0f);
    const CollisionWorld::RayHit hb =
        b.Raycast({-100.0f, 0.0f}, {1.0f, 0.0f}, 1000.0f);
    EXPECT_EQ(ha.hit, hb.hit);
    EXPECT_EQ(ha.handle, hb.handle);
    EXPECT_FLOAT_EQ(ha.t, hb.t);
}

// NOLINTEND(readability-magic-numbers)
