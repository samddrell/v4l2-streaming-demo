"""test-plan.md TC-GUI-02/03: YUY2 -> RGB conversion correctness."""
import numpy as np
import pytest

from yuy2 import yuy2_to_rgb


def _make_yuy2(width: int, height: int, fill) -> bytes:
    """fill(pixel_pair_index) -> (y0, u, y1, v) for each group of 2 pixels."""
    buf = bytearray(width * height * 2)
    pixel_pairs = (width * height) // 2
    for i in range(pixel_pairs):
        y0, u, y1, v = fill(i)
        buf[i * 4 + 0] = y0
        buf[i * 4 + 1] = u
        buf[i * 4 + 2] = y1
        buf[i * 4 + 3] = v
    return bytes(buf)


def test_output_shape_matches_width_height():
    width, height = 8, 4
    data = _make_yuy2(width, height, lambda i: (100, 128, 200, 128))
    rgb = yuy2_to_rgb(data, width, height)
    assert rgb.shape == (height, width, 3)
    assert rgb.dtype == np.uint8


def test_monochrome_uv_128_produces_exact_grayscale():
    # requirements.md §3.1: U/V pinned to 128 for a monochrome image encoded
    # in a YUV format -- this is the actual pixel data every real frame uses.
    width, height = 8, 4
    data = _make_yuy2(width, height, lambda i: (i * 7 % 256, 128, (i * 13 + 50) % 256, 128))
    rgb = yuy2_to_rgb(data, width, height)
    assert np.array_equal(rgb[..., 0], rgb[..., 1])
    assert np.array_equal(rgb[..., 1], rgb[..., 2])


def test_known_pixel_values_black_and_white():
    width, height = 4, 2
    data = _make_yuy2(width, height, lambda i: (0, 128, 255, 128))
    rgb = yuy2_to_rgb(data, width, height)
    assert tuple(rgb[0, 0]) == (0, 0, 0)
    assert tuple(rgb[0, 1]) == (255, 255, 255)
    assert tuple(rgb[1, 0]) == (0, 0, 0)
    assert tuple(rgb[1, 1]) == (255, 255, 255)


def test_wrong_size_raises_value_error():
    with pytest.raises(ValueError):
        yuy2_to_rgb(b"\x00" * 10, 800, 600)
