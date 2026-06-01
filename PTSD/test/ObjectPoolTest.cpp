#include <gtest/gtest.h>

#include <memory>

#include "Util/ObjectPool.hpp"

using Util::ObjectPool;

namespace {

// A trivial payload type with an identity we can track across reuse.
struct Widget {
    int value = 0;
};

} // namespace

TEST(ObjectPoolTest, AcquireFromEmptyCreatesObject) {
    ObjectPool<Widget> pool;

    EXPECT_EQ(pool.FreeCount(), 0u);
    EXPECT_EQ(pool.ActiveCount(), 0u);

    auto obj = pool.Acquire();

    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(pool.ActiveCount(), 1u);
    EXPECT_EQ(pool.FreeCount(), 0u);
    EXPECT_EQ(pool.Capacity(), 1u);
}

TEST(ObjectPoolTest, ReleaseThenAcquireReusesSamePointer) {
    ObjectPool<Widget> pool;

    auto first = pool.Acquire();
    Widget *rawFirst = first.get();
    EXPECT_EQ(pool.ActiveCount(), 1u);

    pool.Release(first);
    EXPECT_EQ(pool.ActiveCount(), 0u);
    EXPECT_EQ(pool.FreeCount(), 1u);

    auto second = pool.Acquire();
    EXPECT_EQ(second.get(), rawFirst); // reuse identity
    EXPECT_EQ(pool.ActiveCount(), 1u);
    EXPECT_EQ(pool.FreeCount(), 0u);
}

TEST(ObjectPoolTest, PrewarmPopulatesFreeList) {
    constexpr std::size_t kPrewarm = 5;
    ObjectPool<Widget> pool(kPrewarm);

    EXPECT_EQ(pool.FreeCount(), kPrewarm);
    EXPECT_EQ(pool.ActiveCount(), 0u);
    EXPECT_EQ(pool.Capacity(), kPrewarm);
}

TEST(ObjectPoolTest, FactoryInvokedOnGrowthNotOnReuse) {
    int factoryCalls = 0;
    ObjectPool<Widget> pool(0, [&factoryCalls] {
        ++factoryCalls;
        return std::make_shared<Widget>();
    });

    // Growth: free list empty, so the factory runs.
    auto a = pool.Acquire();
    EXPECT_EQ(factoryCalls, 1);

    auto b = pool.Acquire();
    EXPECT_EQ(factoryCalls, 2);

    // Reuse: releasing then re-acquiring must NOT call the factory.
    pool.Release(a);
    auto c = pool.Acquire();
    EXPECT_EQ(factoryCalls, 2);
    EXPECT_EQ(c.get(), a.get());

    (void)b;
}

TEST(ObjectPoolTest, PrewarmedAcquireDoesNotCallFactory) {
    int factoryCalls = 0;
    ObjectPool<Widget> pool(3, [&factoryCalls] {
        ++factoryCalls;
        return std::make_shared<Widget>();
    });

    // The 3 prewarmed objects came from the factory during construction.
    EXPECT_EQ(factoryCalls, 3);

    // Acquiring from a populated free list must not grow the pool.
    auto a = pool.Acquire();
    auto b = pool.Acquire();
    auto c = pool.Acquire();
    EXPECT_EQ(factoryCalls, 3);
    EXPECT_EQ(pool.ActiveCount(), 3u);
    EXPECT_EQ(pool.FreeCount(), 0u);

    // The fourth acquire exhausts the free list and grows.
    auto d = pool.Acquire();
    EXPECT_EQ(factoryCalls, 4);

    (void)a;
    (void)b;
    (void)c;
    (void)d;
}

TEST(ObjectPoolTest, CapacityInvariantHoldsAcrossOps) {
    ObjectPool<Widget> pool(2);

    EXPECT_EQ(pool.Capacity(), pool.FreeCount() + pool.ActiveCount());

    auto a = pool.Acquire();
    EXPECT_EQ(pool.Capacity(), pool.FreeCount() + pool.ActiveCount());

    auto b = pool.Acquire();
    auto extra = pool.Acquire(); // forces growth beyond prewarm
    EXPECT_EQ(pool.Capacity(), pool.FreeCount() + pool.ActiveCount());

    pool.Release(a);
    EXPECT_EQ(pool.Capacity(), pool.FreeCount() + pool.ActiveCount());

    pool.Release(b);
    pool.Release(extra);
    EXPECT_EQ(pool.Capacity(), pool.FreeCount() + pool.ActiveCount());
    EXPECT_EQ(pool.ActiveCount(), 0u);
}

TEST(ObjectPoolTest, ReleaseNullptrIsNoOp) {
    ObjectPool<Widget> pool;

    auto obj = pool.Acquire();
    EXPECT_EQ(pool.ActiveCount(), 1u);
    EXPECT_EQ(pool.FreeCount(), 0u);

    std::shared_ptr<Widget> empty;
    pool.Release(empty);

    // Nothing changes: not added to free list, active count untouched.
    EXPECT_EQ(pool.ActiveCount(), 1u);
    EXPECT_EQ(pool.FreeCount(), 0u);

    (void)obj;
}

TEST(ObjectPoolTest, ClearEmptiesFreeListButKeepsActiveCount) {
    ObjectPool<Widget> pool(4);

    auto a = pool.Acquire();
    EXPECT_EQ(pool.FreeCount(), 3u);
    EXPECT_EQ(pool.ActiveCount(), 1u);

    pool.Clear();
    EXPECT_EQ(pool.FreeCount(), 0u);
    EXPECT_EQ(pool.ActiveCount(), 1u);
    EXPECT_EQ(pool.Capacity(), 1u);

    (void)a;
}
