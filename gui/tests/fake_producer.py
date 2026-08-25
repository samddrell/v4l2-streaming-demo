"""Standalone synthetic producer for GUI testing without Jetson hardware
(test-plan.md §8: "gui/tests/fake_producer.py").

Publishes ImageFrame/TelemetryStats messages shaped like the real producer's
output — same ports, same wire format, same drop-oldest/CONFLATE-friendly
behavior — so gui.py can be pointed at 127.0.0.1 and exercised end-to-end.
Not a unit test itself; run directly:

    python tests/fake_producer.py [--rate-hz 30] [--image-port 5555] [--stats-port 5556]
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import numpy as np
import zmq

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from telemetry_pb2 import ImageFrame, PIXEL_FORMAT_YUY2, TelemetryStats  # noqa: E402

WIDTH, HEIGHT = 800, 600

# Same 5x7 digit font/layout as producer/src/synthetic_v4l2_camera.cpp, so
# the rendered counter looks and behaves like the real producer's output.
_GLYPH_W, _GLYPH_H, _GLYPH_SCALE, _GLYPH_SPACING = 5, 7, 6, 1
_MARGIN_X, _MARGIN_Y = 20, 20
_DIGIT_FONT = [
    [0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110],  # 0
    [0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110],  # 1
    [0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111],  # 2
    [0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110],  # 3
    [0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010],  # 4
    [0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110],  # 5
    [0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110],  # 6
    [0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000],  # 7
    [0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110],  # 8
    [0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100],  # 9
]


def _render_frame(timestamp_ms: int) -> bytes:
    """Builds a YUY2 buffer: white background, black ms-since-epoch text,
    U/V pinned to 128 — same content rules as the real camera
    (requirements.md §3.1)."""
    # True YUY2 packs one chroma byte per pixel *pair* (Y0 U Y1 V per 4
    # bytes), not per pixel. This array instead gives every pixel its own
    # [Y, chroma] pair, which produces a byte-identical stream to real YUY2
    # ONLY because chroma is a constant 128 everywhere (U and V are
    # indistinguishable when both are pinned to the same neutral value) —
    # this shortcut would be wrong for any non-neutral chroma.
    buf = np.empty((HEIGHT, WIDTH, 2), dtype=np.uint8)
    buf[..., 0] = 255  # Y: white background
    buf[..., 1] = 128  # chroma: neutral

    digits = str(timestamp_ms)
    for digit_index, ch in enumerate(digits):
        glyph = _DIGIT_FONT[int(ch)]
        cell_x = _MARGIN_X + digit_index * (_GLYPH_W + _GLYPH_SPACING) * _GLYPH_SCALE
        for row in range(_GLYPH_H):
            bits = glyph[row]
            for col in range(_GLYPH_W):
                if not ((bits >> (_GLYPH_W - 1 - col)) & 1):
                    continue
                y0 = _MARGIN_Y + row * _GLYPH_SCALE
                x0 = cell_x + col * _GLYPH_SCALE
                y1 = min(y0 + _GLYPH_SCALE, HEIGHT)
                x1 = min(x0 + _GLYPH_SCALE, WIDTH)
                if y0 >= HEIGHT or x0 >= WIDTH:
                    continue
                buf[y0:y1, x0:x1, 0] = 0  # black

    return buf.tobytes()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rate-hz", type=float, default=30.0)
    parser.add_argument("--image-port", type=int, default=5555)
    parser.add_argument("--stats-port", type=int, default=5556)
    args = parser.parse_args()

    context = zmq.Context()
    image_pub = context.socket(zmq.PUB)
    image_pub.bind(f"tcp://*:{args.image_port}")
    stats_pub = context.socket(zmq.PUB)
    stats_pub.bind(f"tcp://*:{args.stats_port}")

    period_s = 1.0 / args.rate_hz
    print(f"fake_producer publishing on :{args.image_port} (images) / "
          f":{args.stats_port} (telemetry) at {args.rate_hz} Hz — Ctrl+C to stop")

    frame_count = 0
    try:
        while True:
            start = time.monotonic()
            capture_ns = time.time_ns()

            frame = ImageFrame()
            frame.capture_time_unix_ns = capture_ns
            frame.width = WIDTH
            frame.height = HEIGHT
            frame.pixel_format = PIXEL_FORMAT_YUY2
            frame.pixel_data = _render_frame(capture_ns // 1_000_000)
            image_pub.send(frame.SerializeToString())

            stats = TelemetryStats()
            stats.sample_time_unix_ns = time.time_ns()
            stats.queue_depth.current_depth = 0
            stats.queue_depth.min_depth = 0
            stats.queue_depth.max_depth = 0
            stats.queue_depth.mean_depth = 0.0
            stats.time_in_queue.min_ms = 0.0
            stats.time_in_queue.max_ms = 0.0
            stats.time_in_queue.mean_ms = 0.0
            stats.memory_copies_per_frame = 3
            stats.cpu_user_seconds_total = 0.0
            stats.cpu_system_seconds_total = 0.0
            stats.max_rss_kb = 0
            stats.producer_microseconds_per_frame = 0.0
            stats_pub.send(stats.SerializeToString())

            frame_count += 1
            elapsed = time.monotonic() - start
            time.sleep(max(0.0, period_s - elapsed))
    except KeyboardInterrupt:
        pass
    finally:
        print(f"published {frame_count} frames")
        image_pub.close(linger=0)
        stats_pub.close(linger=0)
        context.term()

    return 0


if __name__ == "__main__":
    sys.exit(main())
