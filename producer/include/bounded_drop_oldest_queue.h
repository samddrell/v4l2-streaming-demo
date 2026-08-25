#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

// Depth-stats snapshot returned by BoundedDropOldestQueue::getDepthStats().
struct QueueDepthStats {
  uint32_t current_depth = 0;
  uint32_t min_depth = 0;
  uint32_t max_depth = 0;
  double mean_depth = 0.0;
};

// Bounded, thread-safe, drop-oldest queue: push() evicts the oldest entry
// before inserting a new one once at capacity, so the most recently pushed
// item is never dropped. Each entry is timestamped at push time so callers
// can compute time-in-queue on pop. Owns its own depth-stats snapshot,
// updated under the same mutex that guards push/pop, so there is exactly one
// lock guarding queue depth.
template <typename T>
class BoundedDropOldestQueue {
 public:
  struct PopResult {
    T value;
    std::chrono::steady_clock::time_point enqueued_at;
  };

  explicit BoundedDropOldestQueue(size_t capacity) : capacity_(capacity) {}

  void push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.size() >= capacity_) {
      entries_.pop_front();
    }
    entries_.push_back(Entry{std::move(value), std::chrono::steady_clock::now()});
    updateDepthStatsLocked();
    cv_.notify_one();
  }

  // Blocks until an entry is available or shutdown() is called with an empty
  // queue. Returns std::nullopt only in the latter case.
  std::optional<PopResult> pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !entries_.empty() || shutting_down_; });
    if (entries_.empty()) {
      return std::nullopt;
    }
    Entry entry = std::move(entries_.front());
    entries_.pop_front();
    updateDepthStatsLocked();
    return PopResult{std::move(entry.value), entry.enqueued_at};
  }

  // Wakes any thread blocked in pop() so it can exit cleanly.
  void shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    shutting_down_ = true;
    cv_.notify_all();
  }

  QueueDepthStats getDepthStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
  }

 private:
  struct Entry {
    T value;
    std::chrono::steady_clock::time_point enqueued_at;
  };

  // Called with mutex_ already held.
  void updateDepthStatsLocked() {
    uint32_t depth = static_cast<uint32_t>(entries_.size());
    stats_.current_depth = depth;
    if (sample_count_ == 0) {
      stats_.min_depth = depth;
      stats_.max_depth = depth;
    } else {
      stats_.min_depth = std::min(stats_.min_depth, depth);
      stats_.max_depth = std::max(stats_.max_depth, depth);
    }
    ++sample_count_;
    depth_sum_ += depth;
    stats_.mean_depth = depth_sum_ / static_cast<double>(sample_count_);
  }

  size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Entry> entries_;
  bool shutting_down_ = false;

  QueueDepthStats stats_;
  uint64_t sample_count_ = 0;
  double depth_sum_ = 0.0;
};
