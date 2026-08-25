#include "telemetry.h"

#include <gtest/gtest.h>

TEST(RunningStats, MinMaxMeanOverKnownSequence) {
  RunningStats stats;
  for (double v : {3.0, 1.0, 4.0, 1.0, 5.0}) {
    stats.update(v);
  }
  EXPECT_DOUBLE_EQ(stats.min(), 1.0);
  EXPECT_DOUBLE_EQ(stats.max(), 5.0);
  EXPECT_DOUBLE_EQ(stats.mean(), (3.0 + 1.0 + 4.0 + 1.0 + 5.0) / 5.0);
}

TEST(RunningStats, SingleSample) {
  RunningStats stats;
  stats.update(7.0);
  EXPECT_DOUBLE_EQ(stats.min(), 7.0);
  EXPECT_DOUBLE_EQ(stats.max(), 7.0);
  EXPECT_DOUBLE_EQ(stats.mean(), 7.0);
}

TEST(RunningStats, AllIdenticalSamples) {
  RunningStats stats;
  for (int i = 0; i < 10; ++i) {
    stats.update(2.5);
  }
  EXPECT_DOUBLE_EQ(stats.min(), 2.5);
  EXPECT_DOUBLE_EQ(stats.max(), 2.5);
  EXPECT_DOUBLE_EQ(stats.mean(), 2.5);
}

TEST(RunningStats, ReflectsFullHistoryNotAWindow) {
  RunningStats stats;
  constexpr int kSamples = 100000;
  double sum = 0.0;
  for (int i = 1; i <= kSamples; ++i) {
    stats.update(static_cast<double>(i));
    sum += i;
  }
  EXPECT_DOUBLE_EQ(stats.min(), 1.0);
  EXPECT_DOUBLE_EQ(stats.max(), static_cast<double>(kSamples));
  EXPECT_DOUBLE_EQ(stats.mean(), sum / kSamples);
}
