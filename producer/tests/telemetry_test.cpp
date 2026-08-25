#include "telemetry.h"

#include <gtest/gtest.h>
#include <zmq.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

constexpr size_t kFrameBytes = 800 * 600 * 2;

// Real tcp://127.0.0.1 sockets (not inproc://), per test-plan.md §7 — each
// test uses its own port pair to avoid cross-test collisions.
// Callers must construct the Telemetry object (which binds the PUB socket)
// *before* calling this, then call Telemetry::start() only *after* this
// returns. ZMQ PUB/SUB has a "slow joiner" problem: a PUB won't forward to a
// SUB whose subscription hasn't yet been registered over the connection, so
// the settle wait only means something once there's a live endpoint to
// settle against (design.md §9).
zmq::socket_t makeSubscriber(zmq::context_t& ctx, const std::string& endpoint) {
  zmq::socket_t sub(ctx, zmq::socket_type::sub);
  sub.set(zmq::sockopt::subscribe, "");
  sub.set(zmq::sockopt::rcvtimeo, 5000);
  sub.connect(endpoint);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  return sub;
}

std::unique_ptr<telemetry::ImageFrame> makeFrame(int64_t capture_time_unix_ns) {
  auto frame = std::make_unique<telemetry::ImageFrame>();
  frame->set_capture_time_unix_ns(capture_time_unix_ns);
  frame->set_width(800);
  frame->set_height(600);
  frame->set_pixel_format(telemetry::PIXEL_FORMAT_YUY2);
  frame->set_pixel_data(std::string(kFrameBytes, 0x7F));
  return frame;
}

}  // namespace

TEST(Telemetry, EnqueuePublishRoundTrip) {
  zmq::context_t ctx(1);
  Telemetry telemetry(30, "tcp://127.0.0.1:15561", "tcp://127.0.0.1:15562");
  auto sub = makeSubscriber(ctx, "tcp://127.0.0.1:15561");

  telemetry.start();
  telemetry.enqueue(makeFrame(123456789));

  zmq::message_t msg;
  auto result = sub.recv(msg, zmq::recv_flags::none);
  ASSERT_TRUE(result.has_value());

  telemetry::ImageFrame received;
  ASSERT_TRUE(received.ParseFromArray(msg.data(), msg.size()));
  EXPECT_EQ(received.capture_time_unix_ns(), 123456789);
  EXPECT_EQ(received.width(), 800u);
  EXPECT_EQ(received.height(), 600u);
  EXPECT_EQ(received.pixel_format(), telemetry::PIXEL_FORMAT_YUY2);
  EXPECT_EQ(received.pixel_data().size(), kFrameBytes);

  telemetry.stop();
}

TEST(Telemetry, StatsCadenceMatchesFrameCountWhenNoDrops) {
  zmq::context_t ctx(1);
  constexpr int kFrames = 10;  // well under the 30-frame capacity: no drops
  Telemetry telemetry(30, "tcp://127.0.0.1:15563", "tcp://127.0.0.1:15564");
  auto sub = makeSubscriber(ctx, "tcp://127.0.0.1:15564");

  telemetry.start();
  for (int i = 0; i < kFrames; ++i) {
    telemetry.enqueue(makeFrame(i));
  }

  int received_count = 0;
  for (int i = 0; i < kFrames; ++i) {
    zmq::message_t msg;
    auto result = sub.recv(msg, zmq::recv_flags::none);
    if (!result.has_value()) {
      break;  // timed out
    }
    ++received_count;
  }

  telemetry.stop();
  EXPECT_EQ(received_count, kFrames);
}

TEST(Telemetry, GetrusageFieldsPopulateAndAreNonDecreasing) {
  zmq::context_t ctx(1);
  Telemetry telemetry(30, "tcp://127.0.0.1:15565", "tcp://127.0.0.1:15566");
  auto sub = makeSubscriber(ctx, "tcp://127.0.0.1:15566");

  telemetry.start();
  telemetry.enqueue(makeFrame(1));
  telemetry.enqueue(makeFrame(2));

  telemetry::TelemetryStats first, second;
  zmq::message_t msg1, msg2;
  ASSERT_TRUE(sub.recv(msg1, zmq::recv_flags::none).has_value());
  ASSERT_TRUE(first.ParseFromArray(msg1.data(), msg1.size()));
  ASSERT_TRUE(sub.recv(msg2, zmq::recv_flags::none).has_value());
  ASSERT_TRUE(second.ParseFromArray(msg2.data(), msg2.size()));

  telemetry.stop();

  // max_rss_kb is reliably non-zero on Linux; cpu_*_seconds_total can
  // legitimately read 0.0 over such a short window (getrusage's clock
  // granularity), so only cumulative non-decreasing behavior is asserted
  // for those, per requirements.md §6 decision #7.
  EXPECT_GT(first.max_rss_kb(), 0);
  EXPECT_GT(second.max_rss_kb(), 0);
  EXPECT_GE(second.cpu_user_seconds_total(), first.cpu_user_seconds_total());
  EXPECT_GE(second.cpu_system_seconds_total(), first.cpu_system_seconds_total());
}

TEST(Telemetry, ProducerMicrosecondsPerFrameReflectsMostRecentNotAverage) {
  zmq::context_t ctx(1);
  Telemetry telemetry(30, "tcp://127.0.0.1:15567", "tcp://127.0.0.1:15568");
  auto sub = makeSubscriber(ctx, "tcp://127.0.0.1:15568");

  telemetry.start();
  telemetry.recordProducerFrameTime(std::chrono::microseconds(100));
  telemetry.recordProducerFrameTime(std::chrono::microseconds(500));
  telemetry.enqueue(makeFrame(1));

  zmq::message_t msg;
  ASSERT_TRUE(sub.recv(msg, zmq::recv_flags::none).has_value());
  telemetry::TelemetryStats stats;
  ASSERT_TRUE(stats.ParseFromArray(msg.data(), msg.size()));

  telemetry.stop();
  EXPECT_DOUBLE_EQ(stats.producer_microseconds_per_frame(), 500.0);
}

TEST(Telemetry, MemoryCopiesPerFrameReportsDocumentedConstant) {
  zmq::context_t ctx(1);
  Telemetry telemetry(30, "tcp://127.0.0.1:15569", "tcp://127.0.0.1:15570");
  auto sub = makeSubscriber(ctx, "tcp://127.0.0.1:15570");

  telemetry.start();
  telemetry.enqueue(makeFrame(1));

  zmq::message_t msg;
  ASSERT_TRUE(sub.recv(msg, zmq::recv_flags::none).has_value());
  telemetry::TelemetryStats stats;
  ASSERT_TRUE(stats.ParseFromArray(msg.data(), msg.size()));

  telemetry.stop();
  EXPECT_EQ(stats.memory_copies_per_frame(), 3u);
}

TEST(Telemetry, DropOldestUnderSustainedOverload) {
  zmq::context_t ctx(1);
  constexpr uint32_t kSmallCapacity = 3;
  constexpr int kBurst = 50;
  Telemetry telemetry(kSmallCapacity, "tcp://127.0.0.1:15573", "tcp://127.0.0.1:15574");
  auto sub = makeSubscriber(ctx, "tcp://127.0.0.1:15573");  // image port, not stats

  telemetry.start();

  // Push a burst much larger than capacity, as fast as possible, using
  // full-size frames so the service thread's serialize+send genuinely lags
  // behind the push loop — this is what actually forces drop-oldest
  // evictions rather than a simulated pause.
  for (int i = 0; i < kBurst; ++i) {
    telemetry.enqueue(makeFrame(i));  // capture_time_unix_ns doubles as a sequence number
  }

  int received_count = 0;
  int64_t last_seen = -1;
  while (true) {
    zmq::message_t msg;
    auto result = sub.recv(msg, zmq::recv_flags::none);
    if (!result.has_value()) {
      break;  // timed out: producer is done publishing
    }
    telemetry::ImageFrame frame;
    ASSERT_TRUE(frame.ParseFromArray(msg.data(), msg.size()));
    last_seen = frame.capture_time_unix_ns();
    ++received_count;
  }

  telemetry.stop();

  EXPECT_LT(received_count, kBurst) << "expected some frames to be dropped under overload";
  EXPECT_EQ(last_seen, kBurst - 1) << "the newest enqueued frame must eventually be published";
}

TEST(Telemetry, StopShutsDownCleanlyWithinBoundedTime) {
  Telemetry telemetry(30, "tcp://127.0.0.1:15575", "tcp://127.0.0.1:15576");
  telemetry.start();
  telemetry.enqueue(makeFrame(1));

  std::atomic<bool> stopped{false};
  std::thread stopper([&] {
    telemetry.stop();
    stopped = true;
  });

  bool completed_in_time = false;
  for (int i = 0; i < 100; ++i) {
    if (stopped.load()) {
      completed_in_time = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  stopper.join();

  EXPECT_TRUE(completed_in_time);
}
