"""YUY2 (YUYV, packed 4:2:2) to RGB24 conversion for GUI display.

QImage has no native YUY2 format (design.md §6), so this conversion happens
GUI-side and is not counted in the producer's memory_copies_per_frame.
"""
from __future__ import annotations

import numpy as np

# BT.601 full-range YUV -> RGB coefficients.
_R_V = 1.402
_G_U = 0.344136
_G_V = 0.714136
_B_U = 1.772


def yuy2_to_rgb(pixel_data: bytes, width: int, height: int) -> np.ndarray:
    """Convert a packed YUY2 buffer to an (height, width, 3) uint8 RGB array.

    YUY2 layout: every 4 bytes encode 2 horizontally adjacent pixels as
    Y0 U Y1 V. Since this project pins U/V to 128 (requirements.md §3.1),
    the result is exact grayscale (R == G == B == Y) in practice, but the
    full BT.601 conversion is used so this also works for non-neutral chroma.
    """
    expected_size = width * height * 2
    if len(pixel_data) != expected_size:
        raise ValueError(
            f"expected {expected_size} bytes for {width}x{height} YUY2, got {len(pixel_data)}"
        )

    raw = np.frombuffer(pixel_data, dtype=np.uint8).reshape(-1, 4)  # rows: Y0 U Y1 V
    y = np.empty(width * height, dtype=np.int16)
    y[0::2] = raw[:, 0]
    y[1::2] = raw[:, 2]

    u = np.repeat(raw[:, 1].astype(np.int16), 2) - 128
    v = np.repeat(raw[:, 3].astype(np.int16), 2) - 128

    r = y + _R_V * v
    g = y - _G_U * u - _G_V * v
    b = y + _B_U * u

    rgb = np.clip(np.stack([r, g, b], axis=-1), 0, 255).astype(np.uint8)
    return rgb.reshape(height, width, 3)
