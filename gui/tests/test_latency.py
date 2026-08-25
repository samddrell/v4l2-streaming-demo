"""test-plan.md TC-GUI-04: latency computation."""
from gui import latency_ms


def test_latency_ms_matches_direct_computation():
    capture_ns = 1_000_000_000
    now_ns = capture_ns + 5_000_000  # +5ms
    assert latency_ms(now_ns, capture_ns) == 5.0


def test_latency_ms_zero_when_equal():
    assert latency_ms(42, 42) == 0.0


def test_latency_ms_negative_if_receipt_precedes_capture():
    # Shouldn't happen with synchronized clocks, but the function itself
    # should just compute the difference, not clamp it -- a negative value
    # is a useful signal that clocks are out of sync (requirements.md §6
    # decision #11), not something to silently hide.
    assert latency_ms(0, 5_000_000) == -5.0
