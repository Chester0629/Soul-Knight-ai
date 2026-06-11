#include <gtest/gtest.h>

#include <vector>

#include "sim/Scheduler.hpp"

using Game::Sim::Scheduler;

// NOLINTBEGIN(readability-magic-numbers)

TEST(SchedulerTest, SecondsToTicksRounds) {
    EXPECT_EQ(Scheduler::SecondsToTicks(0.02F), 1);
    EXPECT_EQ(Scheduler::SecondsToTicks(0.10F), 5);
    EXPECT_EQ(Scheduler::SecondsToTicks(0.0F), 0);
    EXPECT_EQ(Scheduler::SecondsToTicks(0.03F), 2); // 1.5 -> 2 (round half up via lround)
}

TEST(SchedulerTest, InvokeFiresOnceAtTheDueTick) {
    Scheduler s;
    int fired = 0;
    int firedAt = -1;
    s.Invoke(3, [&] { ++fired; firedAt = s.CurrentTick(); });
    for (int i = 0; i < 6; ++i) {
        s.Tick();
    }
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(firedAt, 3); // tick 1,2,3 -> fires on the 3rd
}

TEST(SchedulerTest, InvokeRepeatingFiresOnCadence) {
    Scheduler s;
    std::vector<int> ticks;
    s.InvokeRepeating(1, 3, [&] { ticks.push_back(s.CurrentTick()); });
    for (int i = 0; i < 8; ++i) {
        s.Tick();
    }
    EXPECT_EQ(ticks, (std::vector<int>{1, 4, 7}));
}

TEST(SchedulerTest, CancelBeforeDueSuppresses) {
    Scheduler s;
    int fired = 0;
    const Scheduler::Handle h = s.Invoke(3, [&] { ++fired; });
    s.Tick(); // tick 1
    s.Cancel(h);
    for (int i = 0; i < 5; ++i) {
        s.Tick();
    }
    EXPECT_EQ(fired, 0);
}

TEST(SchedulerTest, FifoOrderWithinATick) {
    Scheduler s;
    std::vector<int> order;
    s.Invoke(1, [&] { order.push_back(1); });
    s.Invoke(1, [&] { order.push_back(2); });
    s.Invoke(1, [&] { order.push_back(3); });
    s.Tick();
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3})); // insertion order, deterministic
}

TEST(SchedulerTest, CallbackMayScheduleAnotherCallbackSafely) {
    Scheduler s;
    int fired = 0;
    s.Invoke(1, [&] {
        ++fired;
        s.Invoke(1, [&] { ++fired; }); // due next tick, not this one
    });
    s.Tick(); // tick 1: outer fires (fired=1), inner scheduled for tick 2
    EXPECT_EQ(fired, 1);
    s.Tick(); // tick 2: inner fires
    EXPECT_EQ(fired, 2);
}

TEST(SchedulerTest, InvokeZeroDelayClampedToOne) {
    // A 0 (or negative) delay is clamped to 1 -- it never fires on the current
    // tick, only on the next one.
    Scheduler s;
    int fired = 0;
    s.Invoke(0, [&] { ++fired; });
    EXPECT_EQ(fired, 0); // not this instant
    s.Tick();
    EXPECT_EQ(fired, 1); // fires on tick 1
}

TEST(SchedulerTest, CancelFromWithinCallbackSuppressesRepeating) {
    // The hardest reentrancy path: a repeating callback cancels its own handle
    // mid-tick. It must fire through the self-cancel tick and never again.
    Scheduler s;
    int fired = 0;
    Scheduler::Handle h = 0;
    h = s.InvokeRepeating(1, 1, [&] {
        ++fired;
        if (fired == 2) {
            s.Cancel(h);
        }
    });
    for (int i = 0; i < 5; ++i) {
        s.Tick();
    }
    EXPECT_EQ(fired, 2); // ticks 1 and 2, then self-cancelled
}

// NOLINTEND(readability-magic-numbers)
