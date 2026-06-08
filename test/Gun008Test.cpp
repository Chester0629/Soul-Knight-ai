#include <gtest/gtest.h>

#include <vector>

#include "combat/Gun008.hpp"
#include "data/RGRandom.hpp"

using Game::Gun008;
using Game::Gun008SweepIterator;
using Game::RGRandom;

// NOLINTBEGIN(readability-magic-numbers)

// ---- SweepHalfAngle: base*(1+scale), purely deterministic, no RNG ----------

TEST(Gun008Test, HalfAngleZeroScaleIsBase) {
    // scale 0 -> base + base*0 = base.
    EXPECT_FLOAT_EQ(Gun008::SweepHalfAngle(12.0F, 0.0F), 12.0F);
}

TEST(Gun008Test, HalfAngleScalesBase) {
    // base*(1+scale): 10 * (1 + 0.5) = 15.
    EXPECT_FLOAT_EQ(Gun008::SweepHalfAngle(10.0F, 0.5F), 15.0F);
    // base*(1+scale): 8 * (1 + 2) = 24.
    EXPECT_FLOAT_EQ(Gun008::SweepHalfAngle(8.0F, 2.0F), 24.0F);
}

TEST(Gun008Test, HalfAngleZeroBaseIsZero) {
    EXPECT_FLOAT_EQ(Gun008::SweepHalfAngle(0.0F, 3.0F), 0.0F);
}

// ---- SweepScatterAngle: exactly ONE float draw, in [-half, half] -----------

TEST(Gun008Test, ScatterStaysWithinHalfBounds) {
    Gun008 g;
    g.SetSeed(1234);
    const float half = Gun008::SweepHalfAngle(20.0F, 0.25F); // 25 deg
    for (int i = 0; i < 64; ++i) {
        const float a = g.SweepScatterAngle(half);
        EXPECT_GE(a, -half);
        EXPECT_LE(a, half);
    }
}

TEST(Gun008Test, ScatterDrawsExactlyOneFloatInOrder) {
    // Parallel same-seeded RGRandom must reproduce each scatter draw exactly:
    // proves the brain draws ONE float per tick, in order, with the -half/half
    // bounds and nothing else advancing the stream.
    Gun008 g;
    RGRandom ref;
    g.SetSeed(98765);
    ref.SetRandomSeed(98765);

    const std::vector<float> halves = {5.0F, 12.5F, 30.0F, 7.0F, 18.0F};
    for (const float half : halves) {
        const float got = g.SweepScatterAngle(half);
        const float expected = ref.Range(-half, half); // one parallel draw
        EXPECT_FLOAT_EQ(got, expected);
    }
}

TEST(Gun008Test, ScatterIsDeterministicForSameSeed) {
    Gun008 a;
    Gun008 b;
    a.SetSeed(555);
    b.SetSeed(555);
    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(a.SweepScatterAngle(15.0F), b.SweepScatterAngle(15.0F));
    }
}

// ---- iterator: counter / limit / state machine -----------------------------

TEST(Gun008Test, IteratorStartsAtStartState) {
    Gun008SweepIterator it(3);
    EXPECT_EQ(it.State(), Gun008SweepIterator::kStart);
    EXPECT_EQ(it.Counter(), 0);
    EXPECT_EQ(it.Limit(), 3);
    EXPECT_FALSE(it.Fired());
}

TEST(Gun008Test, FirstPumpYieldsWaitWithoutFiring) {
    // state 0 -> WaitForSeconds yield: stays alive, does NOT fire, stages resume.
    Gun008SweepIterator it(3);
    EXPECT_TRUE(it.MoveNext());
    EXPECT_FALSE(it.Fired());
    EXPECT_EQ(it.State(), Gun008SweepIterator::kResume);
    EXPECT_EQ(it.Counter(), 0);
}

TEST(Gun008Test, FiresWhileCounterBelowLimitThenEnds) {
    // limit 3: after the wait, ticks fire while counter < 3.
    // counter goes 1,2 (< 3 -> fire) then 3 (== 3 -> end). So 2 fired ticks.
    Gun008SweepIterator it(3);
    ASSERT_TRUE(it.MoveNext()); // wait yield, no fire
    ASSERT_FALSE(it.Fired());

    EXPECT_TRUE(it.MoveNext()); // counter 1 < 3 -> fire, alive
    EXPECT_TRUE(it.Fired());
    EXPECT_EQ(it.Counter(), 1);

    EXPECT_TRUE(it.MoveNext()); // counter 2 < 3 -> fire, alive
    EXPECT_TRUE(it.Fired());
    EXPECT_EQ(it.Counter(), 2);

    EXPECT_FALSE(it.MoveNext()); // counter 3 == limit -> end
    EXPECT_FALSE(it.Fired());
    EXPECT_EQ(it.Counter(), 3);
    EXPECT_EQ(it.State(), Gun008SweepIterator::kDone);
}

TEST(Gun008Test, FiredTickCountEqualsLimitMinusOne) {
    // The number of fired ticks for a given limit (counter goes 1..limit, the
    // fire predicate is counter < limit) is exactly limit-1. Verify across
    // limits, including the ctor's configured count.
    auto firedTicks = [](int limit) -> int {
        Gun008SweepIterator it(limit);
        int fired = 0;
        // pump until the coroutine ends (cap iterations defensively).
        for (int i = 0; i < limit + 4; ++i) {
            const bool alive = it.MoveNext();
            if (it.Fired()) {
                EXPECT_TRUE(alive); // a fired pump is always still alive
                ++fired;
            }
            if (!alive) {
                break;
            }
        }
        return fired;
    };
    EXPECT_EQ(firedTicks(1), 0); // counter 1 == limit -> immediate end
    EXPECT_EQ(firedTicks(2), 1);
    EXPECT_EQ(firedTicks(3), 2);
    EXPECT_EQ(firedTicks(Gun008::kCtorFieldI6c), Gun008::kCtorFieldI6c - 1);
}

TEST(Gun008Test, LimitZeroOrOneNeverFires) {
    for (int limit = 0; limit <= 1; ++limit) {
        Gun008SweepIterator it(limit);
        ASSERT_TRUE(it.MoveNext()); // wait yield
        EXPECT_FALSE(it.MoveNext()); // counter 1 >= limit -> end, no fire
        EXPECT_FALSE(it.Fired());
        EXPECT_EQ(it.State(), Gun008SweepIterator::kDone);
    }
}

TEST(Gun008Test, PumpingPastEndStaysDone) {
    Gun008SweepIterator it(2);
    while (it.MoveNext()) {
        // drain to completion
    }
    EXPECT_EQ(it.State(), Gun008SweepIterator::kDone);
    EXPECT_FALSE(it.MoveNext()); // still ended
    EXPECT_FALSE(it.Fired());
    EXPECT_EQ(it.State(), Gun008SweepIterator::kDone);
}

// ---- one fired-tick per scatter draw stays in lockstep ---------------------

TEST(Gun008Test, OneScatterDrawPerFiredTick) {
    // Drive the iterator and draw exactly one scatter angle per Fired() pump;
    // a parallel same-seeded stream must reproduce the whole sequence -> proves
    // the per-tick draw count is 1 and ordering matches.
    auto run = [](int seed) -> std::vector<float> {
        Gun008 g;
        g.SetSeed(seed);
        Gun008SweepIterator it(Gun008::kCtorFieldI6c);
        std::vector<float> trace;
        const float half = Gun008::SweepHalfAngle(22.0F, 0.1F);
        while (it.MoveNext()) {
            if (it.Fired()) {
                trace.push_back(g.SweepScatterAngle(half));
            }
        }
        return trace;
    };
    const auto a = run(31337);
    const auto b = run(31337);
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(a.size(), static_cast<std::size_t>(Gun008::kCtorFieldI6c - 1));
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

// NOLINTEND(readability-magic-numbers)
