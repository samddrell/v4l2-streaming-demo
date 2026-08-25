#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <zmq.hpp>

#include "bounded_drop_oldest_queue.h"
#include "telemetry.pb.h"

// Running min/max/mean over an unbounded sample stream (full run duration,
// no windowing — requirements.md §6 decision #3). Simple sum/count mean, not
// Welford's algorithm (test-plan.md TC-QUEUE-05/TC-STATS-01).
class RunningStats {
 public:
  void update(double value) {
    if (count_ == 0) {
      min_ = value;
      max_ = value;
    } else {
      min_ = std::min(min_, value);
      max_ = std::max(max_, value);
    }
    ++count_;
    sum_ += value;
  }

  double min() const { return min_; }
  double max() const { return max_; }
  double mean() const { return count_ == 0 ? 0.0 : sum_ / static_cast<double>(count_); }
  uint64_t count() const { return count_; }

 private:
  uint64_t count_ = 0;
  double sum_ = 0.0;
  double min_ = 0.0;
  double max_ = 0.0;
};

// Accepts protobuf ImageFrame messages from the producer's main thread,
// queues them (thread-safe, bounded, drop-oldest), and publishes them plus
// derived stats on two ZMQ PUB sockets from its own service thread
// (design.md §5.2).
class Telemetry {
 public:
  Telemetry(size_t queue_capacity, std::string image_endpoint, std::string stats_endpoint);
  // A joinable std::thread destroyed without being joined calls
  // std::terminate(); stop() is always safe to call (start() may never have
  // run), so the destructor calls it unconditionally.
  ~Telemetry() { stop(); }

  void start();  // spawns the service thread
  void stop();

  void enqueue(std::unique_ptr<telemetry::ImageFrame> frame);  // thread-safe, drop-oldest when full
  void recordProducerFrameTime(std::chrono::nanoseconds dt);   // thread-safe counter update

 private:
  void serviceThreadMain();
  void publishStats();

  BoundedDropOldestQueue<std::unique_ptr<telemetry::ImageFrame>> queue_;

  zmq::context_t ctx_;
  zmq::socket_t image_pub_;  // PUB, bound at image_endpoint
  zmq::socket_t stats_pub_;  // PUB, bound at stats_endpoint

  // Written and read exclusively by the service thread — no synchronization
  // needed (design.md §5.2).
  RunningStats time_in_queue_stats_ms_;
  std::atomic<double> last_frame_us_{0};

  std::atomic<bool> running_{false};
  std::thread thread_;
};
