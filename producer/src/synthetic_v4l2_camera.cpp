#include "synthetic_v4l2_camera.h"

#include <chrono>
#include <ctime>
#include <string>

namespace {

// Minimal fixed-width 5x7 bitmap font, digits only (0-9) — the frame content
// is always a decimal milliseconds-since-epoch counter (design.md §4).
// Each row is the top 5 bits (MSB-first) of a byte; bit set = pixel on.
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphScale = 6;
constexpr int kGlyphSpacing = 1;  // cells, before scaling
constexpr int kMarginX = 20;
constexpr int kMarginY = 20;

const uint8_t kDigitFont[10][kGlyphHeight] = {
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},  // 0
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},  // 1
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},  // 2
    {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110},  // 3
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},  // 4
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},  // 5
    {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},  // 6
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},  // 7
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},  // 8
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100},  // 9
};

}  // namespace

SyntheticV4L2Camera::SyntheticV4L2Camera(int width, int height, int fps)
    : width_(width), height_(height), fps_(fps) {}

void SyntheticV4L2Camera::open() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  next_frame_time_ns_ = static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
  uv_initialized_ = false;
}

int64_t SyntheticV4L2Camera::readFrame(FrameBuffer& out) {
  timespec target;
  target.tv_sec = next_frame_time_ns_ / 1'000'000'000LL;
  target.tv_nsec = next_frame_time_ns_ % 1'000'000'000LL;
  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, nullptr);
  next_frame_time_ns_ += static_cast<int64_t>(1e9 / fps_);

  // U/V bytes never change frame to frame — set once, on the first frame
  // after open(), and left alone thereafter (design.md §4).
  if (!uv_initialized_) {
    for (size_t i = 1; i < out.size(); i += 2) {
      out[i] = 128;
    }
    uv_initialized_ = true;
  }

  auto now = std::chrono::system_clock::now();
  int64_t capture_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
  int64_t timestamp_ms = capture_ns / 1'000'000;

  renderClockText(out, timestamp_ms);

  return capture_ns;
}

void SyntheticV4L2Camera::close() {
  // No resources to release for the synthetic camera.
}

void SyntheticV4L2Camera::renderClockText(FrameBuffer& out, int64_t timestamp_ms) {
  // White background: reset every Y-plane byte (even indices) each frame,
  // since the previous frame's text must be cleared before drawing the new
  // value.
  for (size_t i = 0; i < out.size(); i += 2) {
    out[i] = 255;
  }

  std::string digits = std::to_string(timestamp_ms);
  for (size_t digit_index = 0; digit_index < digits.size(); ++digit_index) {
    int digit = digits[digit_index] - '0';
    int cell_x = kMarginX +
                 static_cast<int>(digit_index) * (kGlyphWidth + kGlyphSpacing) * kGlyphScale;
    for (int row = 0; row < kGlyphHeight; ++row) {
      uint8_t bits = kDigitFont[digit][row];
      for (int col = 0; col < kGlyphWidth; ++col) {
        if (!((bits >> (kGlyphWidth - 1 - col)) & 1)) {
          continue;
        }
        for (int sy = 0; sy < kGlyphScale; ++sy) {
          int y = kMarginY + row * kGlyphScale + sy;
          if (y < 0 || y >= height_) {
            continue;
          }
          for (int sx = 0; sx < kGlyphScale; ++sx) {
            int x = cell_x + col * kGlyphScale + sx;
            if (x < 0 || x >= width_) {
              continue;
            }
            size_t byte_index = (static_cast<size_t>(y) * width_ + x) * 2;
            out[byte_index] = 0;  // black
          }
        }
      }
    }
  }
}
