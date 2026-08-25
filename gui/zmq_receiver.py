"""Background ZMQ receiver thread for the v4l2-demo GUI (design.md §6).

Owns both SUB sockets and runs a blocking zmq.Poller loop on a QThread,
deserializing in this thread and emitting parsed protobuf messages via Qt
signals — socket I/O never happens on the Qt main/GUI thread.
"""
from __future__ import annotations

import zmq
from PyQt6.QtCore import QThread, pyqtSignal

from telemetry_pb2 import ImageFrame, TelemetryStats

_POLL_TIMEOUT_MS = 200  # bounds how quickly stop() is noticed


class ZmqReceiver(QThread):
    # Payload type is a protobuf message object; pyqtSignal(object) is the
    # standard way to pass an arbitrary Python object across the queued
    # cross-thread connection Qt sets up automatically here.
    frame_received = pyqtSignal(object)
    stats_received = pyqtSignal(object)

    def __init__(self, host: str, image_port: int, stats_port: int, parent=None):
        super().__init__(parent)
        self._host = host
        self._image_port = image_port
        self._stats_port = stats_port
        self._running = True

    def run(self) -> None:
        context = zmq.Context()

        image_sub = context.socket(zmq.SUB)
        # CONFLATE must be set before connect() — ZMQ silently ignores it
        # otherwise (design.md §6). Only the image socket is conflated: a
        # stale telemetry sample doesn't matter the way a stale frame does.
        image_sub.setsockopt(zmq.CONFLATE, 1)
        image_sub.setsockopt(zmq.SUBSCRIBE, b"")
        image_sub.connect(f"tcp://{self._host}:{self._image_port}")

        stats_sub = context.socket(zmq.SUB)
        stats_sub.setsockopt(zmq.SUBSCRIBE, b"")
        stats_sub.connect(f"tcp://{self._host}:{self._stats_port}")

        poller = zmq.Poller()
        poller.register(image_sub, zmq.POLLIN)
        poller.register(stats_sub, zmq.POLLIN)

        try:
            while self._running:
                events = dict(poller.poll(timeout=_POLL_TIMEOUT_MS))

                if image_sub in events:
                    frame = ImageFrame()
                    frame.ParseFromString(image_sub.recv())
                    self.frame_received.emit(frame)

                if stats_sub in events:
                    stats = TelemetryStats()
                    stats.ParseFromString(stats_sub.recv())
                    self.stats_received.emit(stats)
        finally:
            image_sub.close(linger=0)
            stats_sub.close(linger=0)
            context.term()

    def stop(self) -> None:
        self._running = False
        self.wait()
