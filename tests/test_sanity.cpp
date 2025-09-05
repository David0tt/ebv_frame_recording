#include <gtest/gtest.h>
#include <chrono>

// Simple sanity test to verify GoogleTest integration works
TEST(Sanity, TrueIsTrue) {
    EXPECT_TRUE(true);
}

// Example placeholder for future util tests (will expand in Phase 2)
TEST(Time, SteadyClockProgresses) {
    auto t1 = std::chrono::steady_clock::now();
    auto t2 = std::chrono::steady_clock::now();
    EXPECT_LE(t1, t2);
}
