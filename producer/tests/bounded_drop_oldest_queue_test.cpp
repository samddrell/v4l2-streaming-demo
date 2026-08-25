#include "bounded_drop_oldest_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

TEST(BoundedDropOldestQueue, PushUnderCapacityKeepsAllInFifoOrder) {
  BoundedDropOldestQueue<int> queue(5);
  for (int i = 0; i < 3; ++i) {
    queue.push(i);
  }
  EXPECT_EQ(queue.size(), 3u);
  for (int i = 0; i < 3; ++i) {
    auto result = queue.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, i);
  }
}

TEST(BoundedDropOldestQueue, DropOldestAtCapacity) {
  BoundedDropOldestQueue<int> queue(3);
  for (int i = 0; i < 5; ++i) {  // capacity + 2
    queue.push(i);
  }
  EXPECT_EQ(queue.size(), 3u);
  // Oldest two (0, 1) were dropped; remaining are 2, 3, 4 in order.
  for (int expected = 2; expected <= 4; ++expected) {
    auto result = queue.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, expected);
  }
}

TEST(BoundedDropOldestQueue, PopOrderingIsFifoPostDrop) {
  BoundedDropOldestQueue<int> queue(2);
  queue.push(1);
  queue.push(2);
  queue.push(3);  // drops 1
  auto first = queue.pop();
  auto second = queue.pop();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->value, 2);
  EXPECT_EQ(second->value, 3);
}

TEST(BoundedDropOldestQueue, ConcurrentPushPopNoLossOutsideDropPolicy) {
  // Capacity is deliberately much smaller than the push count: with one
  // thread pushing and one popping, the drop-oldest policy is expected to
  // discard some items. What this test guards is that nothing *else* goes
  // wrong under concurrency — no duplicate or out-of-order delivery. It
  // can't assert an exact delivered count (a legitimate consequence of
  // drop-oldest under contention), only that whatever is delivered is a
  // strictly increasing subsequence.
  BoundedDropOldestQueue<int> queue(100);
  constexpr int kCount = 10000;
  std::vector<int> popped;
  popped.reserve(kCount);

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      queue.push(i);
    }
    queue.shutdown();
  });

  std::thread consumer([&] {
    while (true) {
      auto result = queue.pop();
      if (!result.has_value()) {
        break;  // shutdown() reached with an empty queue: producer is done
      }
      popped.push_back(result->value);
    }
  });

  producer.join();
  consumer.join();

  ASSERT_GT(popped.size(), 0u);
  ASSERT_LE(popped.size(), static_cast<size_t>(kCount));
  EXPECT_EQ(popped.back(), kCount - 1) << "the last pushed item must always survive";
  // Values popped must be strictly increasing (FIFO, no duplication,
  // no re-ordering) even though some were dropped upstream.
  for (size_t i = 1; i < popped.size(); ++i) {
    EXPECT_GT(popped[i], popped[i - 1]);
  }
}

TEST(BoundedDropOldestQueue, GetDepthStatsMatchesHandComputedReference) {
  BoundedDropOldestQueue<int> queue(10);
  queue.push(1);  // depth 1
  queue.push(2);  // depth 2
  queue.push(3);  // depth 3
  queue.pop();     // depth 2
  queue.push(4);  // depth 3

  // Depth sequence observed: 1, 2, 3, 2, 3 -> min 1, max 3, mean 11/5 = 2.2
  QueueDepthStats stats = queue.getDepthStats();
  EXPECT_EQ(stats.min_depth, 1u);
  EXPECT_EQ(stats.max_depth, 3u);
  EXPECT_DOUBLE_EQ(stats.mean_depth, 11.0 / 5.0);
  EXPECT_EQ(stats.current_depth, 3u);
}

TEST(BoundedDropOldestQueue, GetDepthStatsIsThreadSafeUnderConcurrentPush) {
  BoundedDropOldestQueue<int> queue(1000);
  std::atomic<bool> stop{false};

  std::thread producer([&] {
    for (int i = 0; i < 5000; ++i) {
      queue.push(i);
    }
    stop = true;
  });

  std::thread reader([&] {
    while (!stop.load()) {
      auto stats = queue.getDepthStats();
      (void)stats;
    }
  });

  producer.join();
  reader.join();
  SUCCEED();  // Passing (no TSan report) is the assertion for this test.
}

TEST(BoundedDropOldestQueue, BlockingPopWakesOnPush) {
  BoundedDropOldestQueue<int> queue(10);
  auto start = std::chrono::steady_clock::now();

  std::thread pusher([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    queue.push(42);
  });

  auto result = queue.pop();  // blocks until pusher wakes it
  auto elapsed = std::chrono::steady_clock::now() - start;
  pusher.join();

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->value, 42);
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100);
}
