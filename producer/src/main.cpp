#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>

#include "synthetic_v4l2_camera.h"
#include "telemetry.h"
#include "telemetry.pb.h"

namespace {
std::atomic<bool> g_running{true};

void handleSignal(int /*signal*/) { g_running = false; }
}  // namespace

int main() {
  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  using std::chrono::steady_clock;

  SyntheticV4L2Camera camera(800, 600, 30);
  Telemetry telemetry(/*queue_capacity=*/30, "tcp://*:5555", "tcp://*:5556");
  camera.open();
  telemetry.start();

  FrameBuffer buf(800 * 600 * 2);
  while (g_running) {
    int64_t capture_ns = camera.readFrame(buf);  // (1) camera fills buf in place
    auto t0 = steady_clock::now();                // timing starts *after* the camera's
                                                    // pacing wait — see design.md §5.2

    auto pb_frame = std::make_unique<telemetry::ImageFrame>();
    pb_frame->set_capture_time_unix_ns(capture_ns);
    pb_frame->set_width(800);
    pb_frame->set_height(600);
    pb_frame->set_pixel_format(telemetry::PIXEL_FORMAT_YUY2);
    pb_frame->set_pixel_data(buf.data(), buf.size());  // (2) copy: buf -> PB bytes field

    telemetry.enqueue(std::move(pb_frame));  // move: no extra copy
    auto t1 = steady_clock::now();
    telemetry.recordProducerFrameTime(t1 - t0);
  }

  telemetry.stop();
  camera.close();
  return 0;
}
