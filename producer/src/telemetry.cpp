#include "telemetry.h"

#include <sys/resource.h>

#include <chrono>

namespace {

// Fixed constant per design.md §5.2: (1) FrameBuffer -> ImageFrame.pixel_data,
// (2) protobuf SerializeToString, (3) ZMQ send's internal copy into its own
// message buffer (non-zero-copy send, per requirements.md's zero-copy
// non-goal). Documented and audited, not measured at runtime — see
// test-plan.md §9.
constexpr uint32_t kMemoryCopiesPerFrame = 3;

int64_t nowUnixNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

Telemetry::Telemetry(size_t queue_capacity, std::string image_endpoint,
                      std::string stats_endpoint)
    : queue_(queue_capacity),
      ctx_(1),
      image_pub_(ctx_, zmq::socket_type::pub),
      stats_pub_(ctx_, zmq::socket_type::pub) {
  image_pub_.bind(image_endpoint);
  stats_pub_.bind(stats_endpoint);
}

void Telemetry::start() {
  running_ = true;
  thread_ = std::thread(&Telemetry::serviceThreadMain, this);
}

void Telemetry::stop() {
  running_ = false;
  queue_.shutdown();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Telemetry::enqueue(std::unique_ptr<telemetry::ImageFrame> frame) {
  queue_.push(std::move(frame));
}

void Telemetry::recordProducerFrameTime(std::chrono::nanoseconds dt) {
  last_frame_us_.store(std::chrono::duration<double, std::micro>(dt).count(),
                        std::memory_order_relaxed);
}

void Telemetry::serviceThreadMain() {
  while (running_) {
    auto popped = queue_.pop();
    if (!popped) {
      break;  // stop() was called and the queue is empty
    }

    auto time_in_queue = std::chrono::steady_clock::now() - popped->enqueued_at;
    time_in_queue_stats_ms_.update(
        std::chrono::duration<double, std::milli>(time_in_queue).count());

    std::string serialized;
    popped->value->SerializeToString(&serialized);  // copy 2
    image_pub_.send(zmq::buffer(serialized), zmq::send_flags::none);  // copy 3

    publishStats();
  }
}

void Telemetry::publishStats() {
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);

  telemetry::TelemetryStats stats;
  stats.set_sample_time_unix_ns(nowUnixNs());

  QueueDepthStats depth = queue_.getDepthStats();
  auto* qs = stats.mutable_queue_depth();
  qs->set_current_depth(depth.current_depth);
  qs->set_min_depth(depth.min_depth);
  qs->set_max_depth(depth.max_depth);
  qs->set_mean_depth(depth.mean_depth);

  auto* tiq = stats.mutable_time_in_queue();
  tiq->set_min_ms(time_in_queue_stats_ms_.min());
  tiq->set_max_ms(time_in_queue_stats_ms_.max());
  tiq->set_mean_ms(time_in_queue_stats_ms_.mean());

  stats.set_memory_copies_per_frame(kMemoryCopiesPerFrame);
  stats.set_cpu_user_seconds_total(usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6);
  stats.set_cpu_system_seconds_total(usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6);
  stats.set_max_rss_kb(usage.ru_maxrss);
  stats.set_producer_microseconds_per_frame(last_frame_us_.load(std::memory_order_relaxed));

  std::string serialized;
  stats.SerializeToString(&serialized);
  stats_pub_.send(zmq::buffer(serialized), zmq::send_flags::none);
}
