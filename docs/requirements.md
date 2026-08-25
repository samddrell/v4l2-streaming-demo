# Requirements — V4L2 / Protobuf / ZeroMQ Telemetry Demo

> This is a derived/expanded version of [human-requirements.md](human-requirements.md), written to
> resolve ambiguities and make the requirements actionable for design and implementation. The
> original doc is left untouched as the source of intent.

## 1. Purpose

Prototype and measure the cost of using Protocol Buffers to carry image frames over a ZeroMQ
socket. Specifically, quantify:

- The number of memory copies an image frame undergoes from capture to display.
- The minimum achievable end-to-end latency (capture → GUI render) for this pipeline.
- CPU and memory overhead of the producer service.

This is a measurement exercise, not a production system. Correctness of the demo (a counter
visibly incrementing at 30 fps) is the functional acceptance bar; the *interesting* output is the
instrumentation data.

## 2. System Overview

Two processes, two physical machines, same Wi-Fi network:

- **Producer** — C++ service on a Jetson (Linux, ARM). Owns a synthetic V4L2-like camera and a
  telemetry object. Captures frames, wraps them in protobuf messages, publishes them (and
  derived stats) over ZeroMQ. Binds its two `PUB` sockets on all interfaces at fixed,
  well-known ports.
- **GUI** — Python/PyQt application on a Windows laptop. Connects as a `SUB` client to the
  producer's Jetson IP address (supplied via a CLI argument or small config file — no service
  discovery, per non-goals) at the fixed ports, deserializes protobuf messages, and renders the
  image stream and telemetry in real time.

Both devices are assumed to be on the same low-latency Wi-Fi LAN; no NAT/firewall traversal or
network-quality degradation handling is in scope.

```
[V4L2 synthetic camera] -> [Producer main] -> [Telemetry object] -> ZMQ PUB (images)
                                                                   -> ZMQ PUB (telemetry stats)
                                                                          |
                                                                     TCP / network
                                                                          |
                                                              [Python GUI] (Windows)
```

## 3. Functional Requirements

### 3.1 Synthetic V4L2 camera

- Implemented as a C++ class presenting a camera-like interface (`open`, `read`/`dequeue frame`,
  `close`) — not a kernel driver, for phase 1.
- Produces frames at 30 fps, 800x600 resolution.
- Pixel format: **`YUY2` (packed YUV 4:2:2, V4L2 fourcc `YUYV`)**, with U and V bytes held at a
  constant neutral value (e.g. 128) to encode a monochrome image in a YUV format. This is
  deliberately YUV, not a single-plane `GREY`/`Y800` format, so the pipeline exercises a realistic
  packed camera pixel format rather than a synthetic-only one.
- Frame content: white background, black text rendering milliseconds-since-epoch
  (`1970-01-01T00:00:00Z`) as of frame capture time.
- Phase 2 (stretch): re-implement as an actual V4L2-compliant kernel driver / loopback device.

### 3.2 Producer main (Linux)

- Owns one instance of the synthetic V4L2 camera and one instance of the telemetry object.
- Capture loop: pull a frame from the camera, wrap it in a protobuf message together with
  instrumentation fields (see 3.4), and push it into the telemetry object's input queue.
- Runs as a long-lived service (no interactive UI).

### 3.3 Telemetry object

- C++ class. Accepts protobuf image messages and enqueues them.
- Owns its own service thread that drains the queue and publishes.
- The input queue is thread-safe (producer thread enqueues, service thread dequeues
  concurrently), bounded, and **drops the oldest queued frame when full** so the most recent
  frame is always the one sent next (keeps the displayed clock as current as possible under
  backpressure).
- Publishes each image message on **ZMQ socket 1** (`PUB`, image stream).
- Publishes periodic stats on **ZMQ socket 2** (`PUB`, telemetry stream), as a **protobuf
  message** (not free-text) — see 3.4 for required fields.
- Instrumentation collected by the telemetry object, accumulated as running statistics over the
  **full duration of the run** (since the telemetry service thread started — not a sliding/fixed
  window):
  - Queue depth (current, plus running min/max/mean).
  - Time-in-queue per message (running min/max/mean).

### 3.4 Instrumentation / telemetry content

The telemetry protobuf message (published on socket 2) must include, at minimum:

- Time-in-queue statistics: running min, max, mean, covering the full duration of the run (see
  3.3).
- Number of memory copies per frame, from camera buffer to wire send. Phase 1 counts
  **userspace copies only** (there is no kernel-space driver yet): the design must explicitly
  enumerate each copy site (camera → protobuf field, protobuf serialize, ZMQ send) so the count
  is measured, not asserted. Phase 2 (real V4L2 kernel driver) will need to reconsider whether to
  also count the kernel↔userspace copy at capture time.
- CPU usage of the producer service, via `getrusage()` (user+system CPU time), not manual
  `/proc` parsing.
- Memory usage of the producer service (max RSS), via `getrusage()`.
- Microseconds per frame of producer main-thread processing time (protobuf message construction
  through enqueue — see design.md §5.2 for exact scope and rationale; this is one component of
  full pipeline timing, alongside the time-in-queue stats above and the GUI-computed end-to-end
  latency from the image message's capture timestamp).

The image protobuf message (published on socket 1) must include, at minimum:

- The image payload (pixel data + format/width/height metadata sufficient for the GUI to decode
  without out-of-band configuration).
- A capture timestamp, to allow the GUI (or a later analysis step) to compute end-to-end latency.

### 3.5 Python GUI (Windows)

- Built with **PyQt**.
- Connects to the producer's two ZMQ `PUB` sockets (as a `SUB` client) over the network (`tcp://`
  transport — Jetson producer and Windows GUI are different physical machines on the same
  Wi-Fi network). Uses ZMQ's `CONFLATE` socket option on the image subscription so the GUI
  always renders the most recently received frame rather than working through a backlog —
  consistent with the telemetry object's own drop-oldest policy (3.3).
- Deserializes incoming protobuf image messages and renders them in real time. Acceptance
  criterion: the displayed milliseconds-since-1970 counter visibly increments at 30 fps.
- Deserializes and displays the telemetry stats message (queue depth stats, copy count, CPU/mem,
  per-frame timing) in real time, alongside the image.
- No requirement to run on Linux; no requirement for the GUI machine to have a camera or V4L2
  stack.

### 3.6 Documentation

- `docs/` folder contains, at minimum: requirements (this doc + the original), a design doc, and
  a test plan.

## 4. Non-Functional / Cross-Cutting Requirements

- **Wire protocol stability**: producer and GUI must agree on protobuf schema and ZMQ socket
  topology (addresses/ports, PUB/SUB topic filters if any). Schema and endpoint configuration
  should be defined once and shared (e.g. `.proto` files + a small config), not hardcoded
  independently in both codebases.
- **Reproducibility**: language/toolchain versions (C++ standard, protobuf compiler/runtime
  version, ZeroMQ/cppzmq version, Python version, GUI toolkit) should be pinned in the design doc.
- **No persistence requirement**: this is a live-streaming demo; no requirement to record/replay
  frames (unless added later for testing).

## 5. Non-Goals (Phase 1)

- Real V4L2 kernel driver (explicitly phase 2 / bonus).
- Security/authentication on the ZMQ sockets.
- Multi-camera or multi-client fan-out beyond basic PUB/SUB.
- Dynamic reconfiguration of resolution/frame rate at runtime.
- Service discovery / auto-detection of the producer's IP address (out of scope — treated as
  scope creep; the Jetson's IP is supplied to the GUI manually).
- Zero-copy ZMQ send APIs as a comparison point (out of scope — treated as scope creep; only the
  naive copy-based path is measured).
- Network degradation / non-LAN latency handling (both machines are assumed to be on the same
  low-latency Wi-Fi network).
- Pass/fail latency thresholds — the project reports latency numbers; it does not gate on them.

## 6. Resolved Decisions

1. **V4L2 pixel format**: `YUY2` (packed 4:2:2), U/V pinned to a neutral value.
2. **Copy-count methodology**: userspace copies only for phase 1 (no kernel component exists
   yet); revisit for phase 2's real driver.
3. **Reporting window**: running statistics over the full duration of the run, not a sliding or
   fixed-size window.
4. **ZMQ endpoint configuration**: producer's Jetson IP is supplied to the GUI manually
   (CLI arg / config file); fixed well-known ports; no discovery mechanism (non-goal).
5. **Zero-copy ZMQ**: out of scope (non-goal) — only the naive copy-based path is measured.
6. **Backpressure/drop policy**: drop-oldest everywhere — bounded drop-oldest queue in the
   telemetry object, and `ZMQ_CONFLATE` on the GUI's image subscription — so the display always
   shows the most recent frame.
7. **CPU/memory measurement**: `getrusage()` in the producer process, not manual `/proc`
   parsing.
8. **GUI framework**: PyQt.
9. **Success/acceptance threshold**: none — pure latency reporting, no pass/fail bar.
10. **Network assumption**: Jetson and Windows laptop share the same Wi-Fi network, assumed
    low-latency; no degraded-network handling in scope.

11. **Clock synchronization for end-to-end latency**: latency is computed from the **metadata
    capture timestamp already carried in the image protobuf message (3.4)** — the GUI does not
    OCR/read the rendered clock text in the pixels; it diffs its own receipt time against the
    metadata timestamp field. The rendered on-image text remains purely a human-visible
    correctness check (30 fps visibly incrementing), not a data source. Both machines' clocks
    are, for now, **assumed synchronized** (e.g. both NTP-synced); no explicit clock-offset
    calibration is implemented. This is a known limitation to call out in the test plan when
    interpreting cross-machine latency numbers.
12. **PyQt version**: **PyQt6** (current stable default).
13. **Jetson model / OS**: **Jetson Orin Nano**, JetPack 6 / L4T R36.4.3 (Ubuntu 22.04-based,
    aarch64, per `/etc/nv_tegra_release` on-device). Native on-device build targeting
    aarch64/Ubuntu 22.04 toolchain versions.
14. **Bounded queue size**: **1 second's worth of frames** (30 at the target 30 fps).
15. **Telemetry publish rate**: **same rate as the camera feed (30 Hz)** — telemetry is live data
    about current latency/queue state and should update in lockstep with the image stream.
16. **Telemetry wire format**: **protobuf message, not free text** — deviates from
    human-requirements.md's literal wording ("a text string"), because §3.4's telemetry fields
    (running stats, copy count, CPU/mem, per-frame timing) are structured numeric data the GUI
    needs to parse and display live; a protobuf message is the same wire-format discipline
    already used for the image stream, versus inventing a separate text parsing scheme for one
    socket only.
17. **`producer_microseconds_per_frame` scope**: main-thread work only (protobuf construction
    through `enqueue()`), excluding the camera's pacing wait and the telemetry service thread's
    work (serialize, send). Kept intentionally one-directional — no timing data is signaled back
    from the telemetry thread to the main thread. Full pipeline visibility is assembled instead
    from data that already flows forward: the telemetry object's own time-in-queue running stats,
    plus the GUI's end-to-end latency computation from the image message's capture timestamp (see
    design.md §5.2).
