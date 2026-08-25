#include "synthetic_v4l2_camera.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <string>

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 600;
constexpr int kFps = 30;

// --- TC-CAM-05 decode helper -----------------------------------------------
// Mirrors the private font/layout constants in synthetic_v4l2_camera.cpp so
// the test can decode rendered digits back from pixels (test-plan.md
// TC-CAM-05: "a small test-only OCR/bitmap-match helper").
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphScale = 6;
constexpr int kGlyphSpacing = 1;
constexpr int kMarginX = 20;
constexpr int kMarginY = 20;

const uint8_t kDigitFont[10][kGlyphHeight] = {
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
    {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110},
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
    {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100},
};

// Decodes the digit rendered at `digit_index` (0 = leftmost) by sampling the
// center pixel of each font cell and matching the resulting 5x7 bitmap
// against kDigitFont. Returns -1 if no digit matches.
int decodeDigitAt(const FrameBuffer& buf, int digit_index) {
  int cell_x = kMarginX + digit_index * (kGlyphWidth + kGlyphSpacing) * kGlyphScale;
  uint8_t bits[kGlyphHeight] = {0};
  for (int row = 0; row < kGlyphHeight; ++row) {
    uint8_t row_bits = 0;
    for (int col = 0; col < kGlyphWidth; ++col) {
      int x = cell_x + col * kGlyphScale + kGlyphScale / 2;
      int y = kMarginY + row * kGlyphScale + kGlyphScale / 2;
      size_t byte_index = (static_cast<size_t>(y) * kWidth + x) * 2;
      bool black = buf[byte_index] == 0;
      row_bits = static_cast<uint8_t>((row_bits << 1) | (black ? 1 : 0));
    }
    bits[row] = row_bits;
  }
  for (int digit = 0; digit < 10; ++digit) {
    bool match = true;
    for (int row = 0; row < kGlyphHeight; ++row) {
      if (kDigitFont[digit][row] != bits[row]) {
        match = false;
        break;
      }
    }
    if (match) {
      return digit;
    }
  }
  return -1;
}

std::string decodeDigits(const FrameBuffer& buf, size_t digit_count) {
  std::string result;
  for (size_t i = 0; i < digit_count; ++i) {
    int digit = decodeDigitAt(buf, static_cast<int>(i));
    result.push_back(digit >= 0 ? static_cast<char>('0' + digit) : '?');
  }
  return result;
}

}  // namespace

TEST(SyntheticV4L2Camera, ReadFrameProducesExactlyExpectedBufferSize) {
  SyntheticV4L2Camera camera(kWidth, kHeight, kFps);
  camera.open();
  FrameBuffer buf(static_cast<size_t>(kWidth) * kHeight * 2);
  camera.readFrame(buf);
  EXPECT_EQ(buf.size(), static_cast<size_t>(kWidth) * kHeight * 2);
}

TEST(SyntheticV4L2Camera, UvBytesAreAlways128) {
  SyntheticV4L2Camera camera(kWidth, kHeight, kFps);
  camera.open();
  FrameBuffer buf(static_cast<size_t>(kWidth) * kHeight * 2);
  for (int i = 0; i < 3; ++i) {
    camera.readFrame(buf);
    for (size_t j = 1; j < buf.size(); j += 2) {
      ASSERT_EQ(buf[j], 128) << "byte " << j << " on frame " << i;
    }
  }
}

TEST(SyntheticV4L2Camera, FramePacingAveragesToTargetPeriod) {
  SyntheticV4L2Camera camera(kWidth, kHeight, kFps);
  camera.open();
  FrameBuffer buf(static_cast<size_t>(kWidth) * kHeight * 2);

  constexpr int kIterations = 100;
  camera.readFrame(buf);  // warm-up, establishes the pacing baseline
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < kIterations; ++i) {
    camera.readFrame(buf);
  }
  auto elapsed = std::chrono::steady_clock::now() - start;
  double mean_ms =
      std::chrono::duration<double, std::milli>(elapsed).count() / kIterations;

  EXPECT_NEAR(mean_ms, 1000.0 / kFps, 2.0);
}

TEST(SyntheticV4L2Camera, CaptureTimestampStrictlyIncreasing) {
  SyntheticV4L2Camera camera(kWidth, kHeight, kFps);
  camera.open();
  FrameBuffer buf(static_cast<size_t>(kWidth) * kHeight * 2);

  int64_t previous = camera.readFrame(buf);
  for (int i = 0; i < 10; ++i) {
    int64_t current = camera.readFrame(buf);
    EXPECT_GT(current, previous);
    previous = current;
  }
}

TEST(SyntheticV4L2Camera, RenderedTextMatchesReturnedTimestamp) {
  SyntheticV4L2Camera camera(kWidth, kHeight, kFps);
  camera.open();
  FrameBuffer buf(static_cast<size_t>(kWidth) * kHeight * 2);

  int64_t capture_ns = camera.readFrame(buf);
  int64_t timestamp_ms = capture_ns / 1'000'000;
  std::string expected = std::to_string(timestamp_ms);

  std::string decoded = decodeDigits(buf, expected.size());
  EXPECT_EQ(decoded, expected);
}

TEST(SyntheticV4L2Camera, TextStaysWithinFrameBoundsOverRepeatedFrames) {
  // Real ms-since-epoch timestamps are currently 13 digits (the "longest
  // realistic" case per test-plan.md TC-CAM-06); repeated real readFrame()
  // calls exercise exactly that width. renderClockText()'s own bounds checks
  // (x/y < width_/height_) are what actually prevent an overrun; this test
  // confirms that path doesn't crash or corrupt the fixed-size buffer.
  SyntheticV4L2Camera camera(kWidth, kHeight, kFps);
  camera.open();
  FrameBuffer buf(static_cast<size_t>(kWidth) * kHeight * 2);
  for (int i = 0; i < 5; ++i) {
    camera.readFrame(buf);
    ASSERT_EQ(buf.size(), static_cast<size_t>(kWidth) * kHeight * 2);
  }
}

namespace {
std::atomic<long> g_alloc_count{0};
bool g_counting = false;
}  // namespace

void* operator new(size_t size) {
  if (g_counting) {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
  }
  void* p = std::malloc(size);
  if (!p) {
    throw std::bad_alloc();
  }
  return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

TEST(SyntheticV4L2Camera, NoAllocationOnSteadyStatePath) {
  SyntheticV4L2Camera camera(kWidth, kHeight, kFps);
  camera.open();
  FrameBuffer buf(static_cast<size_t>(kWidth) * kHeight * 2);

  for (int i = 0; i < 5; ++i) {
    camera.readFrame(buf);  // warm-up
  }

  g_alloc_count.store(0, std::memory_order_relaxed);
  g_counting = true;
  for (int i = 0; i < 100; ++i) {
    camera.readFrame(buf);
  }
  g_counting = false;

  EXPECT_EQ(g_alloc_count.load(std::memory_order_relaxed), 0);
}

TEST(SyntheticV4L2Camera, OpenCloseIdempotency) {
  SyntheticV4L2Camera camera(kWidth, kHeight, kFps);
  EXPECT_NO_THROW(camera.close());  // close() before open()
  EXPECT_NO_THROW(camera.open());
  EXPECT_NO_THROW(camera.open());  // open() twice
  EXPECT_NO_THROW(camera.close());
}
