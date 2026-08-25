#pragma once

#include <cstdint>
#include <vector>

// Pre-allocated, reusable pixel buffer owned by the caller and passed by
// reference to readFrame(); sized width*height*2 for YUY2. The camera never
// allocates one itself.
using FrameBuffer = std::vector<uint8_t>;

// A synthetic camera presenting a V4L2-like interface, but backed by a plain
// C++ class rather than a kernel driver (phase 1, per requirements.md §3.1).
// Produces YUY2 frames: white background, black text rendering the
// milliseconds-since-epoch at capture time, U/V bytes pinned to 128.
class SyntheticV4L2Camera {
 public:
  SyntheticV4L2Camera(int width, int height, int fps);

  void open();

  // Blocks until the next frame's target capture time, then fills `out` in
  // place and returns the capture timestamp (nanoseconds since epoch). No
  // allocation on the steady-state path.
  int64_t readFrame(FrameBuffer& out);

  void close();

 private:
  void renderClockText(FrameBuffer& out, int64_t timestamp_ms);

  int width_, height_, fps_;
  int64_t next_frame_time_ns_ = 0;
  bool uv_initialized_ = false;
};
