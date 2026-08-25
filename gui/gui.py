"""PyQt6 GUI entry point for the v4l2-demo project (design.md §6).

Connects to the producer's two ZMQ PUB sockets, renders the live YUY2 image
stream and telemetry panel. No requirement to run on Linux or have a camera
(requirements.md §3.5).
"""
from __future__ import annotations

import argparse
import sys
import time

import numpy as np
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QImage, QPixmap
from PyQt6.QtWidgets import (
    QApplication,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QWidget,
)

from yuy2 import yuy2_to_rgb
from zmq_receiver import ZmqReceiver

DEFAULT_IMAGE_PORT = 5555
DEFAULT_STATS_PORT = 5556

_STAT_ROWS = (
    ("queue_depth", "Queue depth (cur / min / max / mean)"),
    ("time_in_queue", "Time in queue, ms (min / max / mean)"),
    ("copies", "Memory copies / frame"),
    ("cpu", "CPU time, s (user / system)"),
    ("rss", "Max RSS (KB)"),
    ("producer_us", "Producer µs/frame"),
    ("latency", "End-to-end latency (ms)"),
)


def latency_ms(now_unix_ns: int, capture_time_unix_ns: int) -> float:
    """GUI-side end-to-end latency: receipt time minus the image's capture
    timestamp (requirements.md §6 decision #11) — never derived from pixel
    content. Both machines' clocks are assumed synchronized; no calibration
    is implemented (documented limitation)."""
    return (now_unix_ns - capture_time_unix_ns) / 1e6


class MainWindow(QMainWindow):
    def __init__(self, host: str, image_port: int, stats_port: int):
        super().__init__()
        self.setWindowTitle(f"v4l2-demo — {host}:{image_port}/{stats_port}")

        self._image_label = QLabel("waiting for frames…")
        self._image_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._image_label.setMinimumSize(800, 600)

        self._stat_labels: dict[str, QLabel] = {}
        stats_form = QFormLayout()
        for key, title in _STAT_ROWS:
            label = QLabel("—")
            self._stat_labels[key] = label
            stats_form.addRow(title, label)
        stats_widget = QWidget()
        stats_widget.setLayout(stats_form)

        central = QWidget()
        layout = QHBoxLayout(central)
        layout.addWidget(self._image_label, stretch=3)
        layout.addWidget(stats_widget, stretch=1)
        self.setCentralWidget(central)

        self._receiver = ZmqReceiver(host, image_port, stats_port)
        self._receiver.frame_received.connect(self._on_frame)
        self._receiver.stats_received.connect(self._on_stats)
        self._receiver.start()

    def closeEvent(self, event) -> None:  # noqa: N802 (Qt override naming)
        self._receiver.stop()
        super().closeEvent(event)

    def _on_frame(self, frame) -> None:
        try:
            rgb = yuy2_to_rgb(frame.pixel_data, frame.width, frame.height)
        except ValueError:
            return  # malformed/short frame — skip rather than crash the GUI

        image = QImage(
            rgb.data,
            frame.width,
            frame.height,
            frame.width * 3,
            QImage.Format.Format_RGB888,
        ).copy()  # detach from the numpy buffer before it's overwritten/freed
        self._image_label.setPixmap(
            QPixmap.fromImage(image).scaled(
                self._image_label.size(),
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
        )

        now_ns = time.time_ns()
        self._stat_labels["latency"].setText(
            f"{latency_ms(now_ns, frame.capture_time_unix_ns):.2f}"
        )

    def _on_stats(self, stats) -> None:
        qd = stats.queue_depth
        self._stat_labels["queue_depth"].setText(
            f"{qd.current_depth} / {qd.min_depth} / {qd.max_depth} / {qd.mean_depth:.2f}"
        )
        tiq = stats.time_in_queue
        self._stat_labels["time_in_queue"].setText(
            f"{tiq.min_ms:.2f} / {tiq.max_ms:.2f} / {tiq.mean_ms:.2f}"
        )
        self._stat_labels["copies"].setText(str(stats.memory_copies_per_frame))
        self._stat_labels["cpu"].setText(
            f"{stats.cpu_user_seconds_total:.2f} / {stats.cpu_system_seconds_total:.2f}"
        )
        self._stat_labels["rss"].setText(str(stats.max_rss_kb))
        self._stat_labels["producer_us"].setText(
            f"{stats.producer_microseconds_per_frame:.1f}"
        )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="v4l2-demo GUI (design.md §6)")
    parser.add_argument("--host", required=True, help="Producer's Jetson IP address")
    parser.add_argument("--image-port", type=int, default=DEFAULT_IMAGE_PORT)
    parser.add_argument("--stats-port", type=int, default=DEFAULT_STATS_PORT)
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    app = QApplication(sys.argv[:1])  # parsed our own args above; don't let Qt consume them
    window = MainWindow(args.host, args.image_port, args.stats_port)
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
