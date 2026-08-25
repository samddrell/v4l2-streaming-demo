# Test Plan — V4L2 / Protobuf / ZeroMQ Telemetry Demo

> Implements [requirements.md](requirements.md) and [design.md](design.md). Read those first —
> this doc defines how each requirement gets verified once code exists. No code has been written
> yet (per `CLAUDE.md`); this plan is written against the current design so implementation and
> tests can be built together.

## 1. Purpose & Scope

Verify that the producer and GUI meet requirements.md's functional requirements (§3) and that the
concrete design in design.md is implemented correctly — particularly the two things this project
exists to measure: **memory-copy count** and **end-to-end latency** — plus the thread-safety and
wire-protocol correctness that measurement depends on.

This is a measurement prototype, not a production system (requirements.md §1). Consistent with
that, this plan **reports** performance numbers (latency, CPU, memory) rather than gating on
pass/fail thresholds (requirements.md §5, non-goal). Correctness bar for functional behavior is
binary: the acceptance test in §11 (visible 30fps counter) either passes or it doesn't.

## 2. Test Levels & Tooling

| Level | What it covers | Tooling | Runs on |
|---|---|---|---|
| Unit — producer | Individual C++ classes in isolation | GoogleTest (per prior decision) | Jetson (native build) or any Linux dev box for iteration |
| Unit — GUI | Individual Python functions/classes in isolation | `pytest` | Windows |
| Wire-protocol | `.proto`-generated bindings agree between C++ and Python | GoogleTest + `pytest`, shared fixture files | Both |
| Integration — producer | Camera → Telemetry → ZMQ, single process, real threads | GoogleTest, real `tcp://127.0.0.1` sockets | Jetson |
| Integration — GUI | `ZmqReceiver` against a scripted test publisher | `pytest`, a small standalone Python ZMQ publisher fixture | Windows |
| System / E2E | Full producer (Jetson) + full GUI (Windows) over real Wi-Fi | Manual + a scripted log-capture helper | Both, networked |
| Manual acceptance | The one functional bar requirements.md sets | Human observation | Both |

**Test directory layout** (matches design.md §8's build layout):
```
producer/tests/            (GoogleTest sources, one file per class under test)
gui/tests/                 (pytest sources, one file per module under test)
```

## 3. Test Environment & Prerequisites

- **Jetson Orin Nano**, JetPack 6 / L4T R36.4.3, on the same Wi-Fi LAN as the Windows machine, with
  the toolchain from design.md §2 installed (CMake ≥3.16, `libprotobuf-dev`/`protobuf-compiler`,
  `libzmq3-dev`, `cppzmq`, GoogleTest via apt or `FetchContent`).
- **Windows laptop** (this machine), Python 3.10+, with `gui/requirements.txt` installed
  (PyQt6, `pyzmq`, `protobuf`, `numpy`, plus `pytest` for the test suite).
- Both machines can reach each other by IP over Wi-Fi; firewall allows inbound TCP on 5555/5556 on
  the Jetson (Windows Defender / Jetson `ufw` if enabled — not covered by design, note as a setup
  step here since it will otherwise silently look like a network/latency problem).
- `protoc` version pinned and matching between the C++ build and the Python-generated `_pb2.py`
  (design.md §2) — a version mismatch here is a plausible early failure mode, worth an explicit
  check (§6, TC-WIRE-01) rather than assuming it "just works."

## 4. Unit Tests — Producer (C++, GoogleTest)

### 4.1 `SyntheticV4L2Camera`

| ID | Case | Expected result |
|---|---|---|
| TC-CAM-01 | `readFrame` output buffer size | Buffer is exactly `width * height * 2` bytes (YUY2) |
| TC-CAM-02 | U/V bytes | Every odd-indexed byte (U/V positions) in the YUY2 buffer equals 128, on every frame |
| TC-CAM-03 | Frame pacing | Over N=100 calls to `readFrame`, mean inter-call interval is 33.3ms ± a documented jitter tolerance (e.g. ±2ms), using `CLOCK_MONOTONIC` timestamps taken by the test, not the camera's own reported timestamp |
| TC-CAM-04 | Capture timestamp monotonicity | `readFrame`'s returned `capture_ns` is strictly increasing call over call |
| TC-CAM-05 | Text rendering — content | Rendered milliseconds-since-epoch string in the Y-plane, decoded back via a small test-only OCR/bitmap-match helper, matches the timestamp `readFrame` returned (within one frame period) |
| TC-CAM-06 | Text rendering — placement stability | Text region stays within frame bounds at both a "short" timestamp string and the longest realistic one (13 digits), i.e. no buffer overrun as digit count grows |
| TC-CAM-07 | No per-frame allocation | Run under a heap-allocation counter (e.g. a custom `operator new` counting shim, or Valgrind `--tool=massif`/`heaptrack`) across 100 `readFrame` calls after the first; assert zero allocations after warm-up, per design.md §4's "no allocation on the steady-state path" claim |
| TC-CAM-08 | `open`/`close` idempotency | Calling `close()` without `open()`, or `open()` twice, doesn't crash or corrupt state (defensive test — design.md doesn't specify behavior here, so this test also serves to pin it down) |

### 4.2 `BoundedDropOldestQueue`

This class now owns queue-depth statistics directly (per the corrected design), so its tests cover
both container behavior and stats correctness together.

| ID | Case | Expected result |
|---|---|---|
| TC-QUEUE-01 | Push under capacity | Pushing N < capacity items keeps all N, in FIFO order |
| TC-QUEUE-02 | Drop-oldest at capacity | Pushing capacity+1 items drops exactly the oldest; the remaining items are the most recent `capacity` in original order |
| TC-QUEUE-03 | Pop ordering | Pop always returns items in the order they were successfully retained (FIFO, post-drop) |
| TC-QUEUE-04 | Concurrent push/pop | One thread pushes 10,000 items while another pops continuously; run under ThreadSanitizer (`-fsanitize=thread`); assert no data race reported and no item is ever double-popped or lost outside of the documented drop-oldest policy |
| TC-QUEUE-05 | `getDepthStats()` correctness | After a scripted sequence of pushes/pops with known depths at each step, `getDepthStats()` returns min/max/mean matching a hand-computed reference (simple sum/count mean — no Welford, per current design) |
| TC-QUEUE-06 | `getDepthStats()` thread safety | Call `getDepthStats()` from a second thread concurrently with pushes from the producer thread under ThreadSanitizer; assert no race — this is the test that directly verifies the mutex fix (single queue-owned mutex guarding both the container and its depth stats) |
| TC-QUEUE-07 | Blocking pop wakes on push | A thread blocked in pop (empty queue) is woken within a bounded time (e.g. <10ms) of a push via the condition variable, not via polling/timeout |

### 4.3 `RunningStats` (time-in-queue)

| ID | Case | Expected result |
|---|---|---|
| TC-STATS-01 | Min/max/mean over a known sample sequence | Matches hand-computed values for a fixed input sequence, including edge cases (single sample, all-identical samples) |
| TC-STATS-02 | No windowing | Feed 100,000 samples; assert the running mean reflects the *full* history, not a truncated/windowed subset (guards against an accidental sliding-window regression, per requirements.md §6 decision #3) |
| TC-STATS-03 | Single-thread-only access assumption | Not a runtime test — a code-review checklist item (§9) confirming `time_in_queue_stats_ms_` is never touched outside the service thread, since the design deliberately relies on that invariant instead of a lock |

### 4.4 `Telemetry` class (integration-flavored, still within producer-only scope)

| ID | Case | Expected result |
|---|---|---|
| TC-TEL-01 | Enqueue → publish round trip | Enqueue a known `ImageFrame`; a test subscriber on `inproc://` or `tcp://127.0.0.1` receives a message that deserializes back to the same field values |
| TC-TEL-02 | Stats cadence, no-backpressure case | Enqueue N frames slowly enough that none are dropped (stay under the 30-frame cap, let the service thread fully drain between enqueues); assert exactly N `TelemetryStats` messages are published. Publishing is actually one-per-dequeue-and-send (requirements.md §6 decision #15), not one-per-enqueue, so this only holds when no drop-oldest eviction occurs — see TC-TEL-06 for the drop case, where published count is expected to be less than N |
| TC-TEL-03 | `getrusage()` fields populate | `cpu_user_seconds_total`, `cpu_system_seconds_total`, `max_rss_kb` are non-zero and non-decreasing across successive stats messages (cumulative counters must never go backward) |
| TC-TEL-04 | `producer_microseconds_per_frame` reflects most recent, not averaged | Feed two `recordProducerFrameTime` calls with distinct values; assert the next published stats message reports the second value, not an average of both (per design.md §5.2) |
| TC-TEL-05 | `memory_copies_per_frame` reports the documented constant | Field value is always `3` (see §9 for how the constant itself is verified) |
| TC-TEL-06 | Drop-oldest under sustained overload | Enqueue faster than the service thread can drain (e.g. block the service thread briefly) past the 30-frame cap; assert the oldest frames are dropped and the *newest* enqueued frame is eventually the one published, not a stale one |
| TC-TEL-07 | `stop()` shuts down cleanly | Service thread exits within a bounded time after `stop()`, no deadlock, no dropped-but-unprocessed frame is instead force-published |

## 5. Unit Tests — GUI (Python, `pytest`)

| ID | Case | Expected result |
|---|---|---|
| TC-GUI-01 | `ImageFrame` deserialization | A known serialized byte string deserializes to expected field values |
| TC-GUI-02 | YUY2 → RGB conversion correctness | A synthetic YUY2 buffer with known Y/U/V values converts to the expected RGB pixels (spot-check corner and center pixels against a hand-computed reference) |
| TC-GUI-03 | YUY2 → RGB conversion, monochrome case | Since U/V are always 128 in this project (requirements.md §3.1), assert the converted image is exactly greyscale (R=G=B at every pixel) for a representative frame |
| TC-GUI-04 | Latency computation | Given a fixed `capture_time_unix_ns` and a mocked "current time," `latency_ms` matches `(now - capture) / 1e6` exactly |
| TC-GUI-05 | `ZmqReceiver` doesn't block the Qt thread | Instantiate `ZmqReceiver` against a socket with no publisher connected; assert the Qt main thread's event loop continues processing (e.g. a timer callback still fires) while the receiver thread blocks in its poller |
| TC-GUI-06 | `CONFLATE` set pre-connect | Code-level check (not behavioral — can't easily observe `CONFLATE`'s effect in a unit test) that `zmq.CONFLATE` is set on the image socket *before* `connect()` is called, per design.md §6 (ZMQ silently ignores the option if set after) |
| TC-GUI-07 | Stats panel field mapping | Given a `TelemetryStats` message with known field values, the panel's displayed strings/labels contain the correct values (queue depth min/max/mean, time-in-queue min/max/mean, copies/frame, CPU time, RSS, producer µs/frame, latency) |

## 6. Wire-Protocol / Cross-Language Compatibility Tests

| ID | Case | Expected result |
|---|---|---|
| TC-WIRE-01 | `protoc` version match | Document the exact `protoc` version used for both the CMake-generated C++ bindings and the Python `_pb2.py`; a version mismatch is a known failure mode (design.md §2) and should be checked, not assumed |
| TC-WIRE-02 | Cross-language round trip | A C++ test program serializes a known `ImageFrame` and `TelemetryStats` to files; a Python test deserializes both files and asserts every field matches expected values (and vice versa: Python serializes, C++ deserializes) |
| TC-WIRE-03 | `PixelFormat` enum agreement | Both sides agree that `PIXEL_FORMAT_YUY2 == 1`; guards against enum drift if the `.proto` is ever regenerated inconsistently |
| TC-WIRE-04 | Unknown/default field handling | A message missing an optional-in-practice field (e.g. constructed with defaults) still deserializes without error on the other language's side |

## 7. Integration Tests — Producer (single machine)

Run the full producer process (camera + telemetry, real threads, real `tcp://127.0.0.1` ZMQ
sockets — not `inproc://`, to exercise the actual serialization/network code path) against a
lightweight test subscriber.

| ID | Case | Expected result |
|---|---|---|
| TC-INT-P-01 | Sustained run, frame count | Over a 30-second run at 30fps, the test subscriber receives frame count within an acceptable tolerance of 900 (accounting for legitimate drop-oldest behavior under any transient backpressure, not lost/corrupted messages) |
| TC-INT-P-02 | No crashes/leaks over sustained run | 5-minute run under Valgrind (`--tool=memcheck`) or ASan build; zero leaks, zero invalid accesses |
| TC-INT-P-03 | CPU/memory overhead is stable | `max_rss_kb` plateaus (doesn't grow unbounded) over a 5-minute run — this is the practical check that the "no Welford, simple running stats" simplification (per this session's change) doesn't in fact leak or grow memory in a way that would have justified the online algorithm in the first place |
| TC-INT-P-04 | Graceful shutdown (SIGINT/SIGTERM) | Producer exits cleanly on signal, no hung threads, no zombie ZMQ context |

## 8. Integration Tests — GUI (against a scripted test publisher)

A small standalone Python script (`gui/tests/fake_producer.py`) publishes synthetic `ImageFrame`
and `TelemetryStats` messages at a controllable rate, standing in for the real Jetson producer so
GUI integration tests don't require hardware.

| ID | Case | Expected result |
|---|---|---|
| TC-INT-G-01 | End-to-end render | GUI connects to the fake producer; the displayed image updates and the visible ms-counter text (rendered by the fake producer, matching real producer behavior) increments frame over frame |
| TC-INT-G-02 | `CONFLATE` behavior under a slow consumer | Fake producer publishes faster than the GUI can render (simulate slow rendering with an injected delay); assert the GUI always ends up displaying the *latest* available frame, never falls behind and "catches up" through a backlog |
| TC-INT-G-03 | Reconnect after producer restart | Stop and restart the fake producer mid-test (new ZMQ bind); GUI's `SUB` socket reconnects automatically (ZMQ's default behavior) and resumes rendering without requiring the GUI process to restart |
| TC-INT-G-04 | GUI startup before producer is running | Start the GUI with no producer yet publishing; assert no crash, and frames begin appearing once the fake producer starts (validates graceful "nothing yet" state, not just the happy path) |

## 9. Copy-Count Verification (code-audit method)

Per design.md §5.2, `memory_copies_per_frame` is a **documented constant (3)**, not a
runtime-measured value — verified by code audit rather than instrumentation. This test plan
formalizes that audit as a repeatable checklist rather than leaving it as a one-time claim:

| ID | Checklist item | Pass criterion |
|---|---|---|
| TC-COPY-01 | Camera fill site | `SyntheticV4L2Camera::readFrame` writes directly into the caller-owned `FrameBuffer` with no intermediate buffer — confirm via reading `synthetic_v4l2_camera.cpp` at implementation time |
| TC-COPY-02 | Copy 1 site | `ImageFrame::set_pixel_data(buf.data(), buf.size())` is the only place `FrameBuffer` bytes are copied into the protobuf message |
| TC-COPY-03 | Copy 2 site | `SerializeToArray`/`SerializeToString` is called exactly once per frame, immediately before the ZMQ send, with no intermediate `std::string` copy of the serialized bytes in between |
| TC-COPY-04 | Copy 3 site | `image_pub_.send()` uses the default (copying) `zmq::message_t` construction path — confirm no `zmq::message_t` move-from-existing-buffer / zero-copy constructor is used anywhere (would contradict requirements.md §5's zero-copy non-goal and invalidate the count) |
| TC-COPY-05 | No hidden 4th copy | Re-audit after implementation for compiler-inserted copies that could defeat RVO/move semantics (e.g. `pb_frame` passed by value instead of `std::move`d into `enqueue`) — re-run this checklist any time `main.cpp` or `telemetry.cpp`'s frame-handling code changes |

This checklist should be re-executed (not just trusted from this document) once the producer code
actually exists, since it's verifying real source, not design intent.

## 10. Stress / Backpressure Tests

| ID | Case | Expected result |
|---|---|---|
| TC-STRESS-01 | Producer under CPU contention | Pin the producer to a starved CPU (e.g. `taskset` to one core shared with a `stress-ng` load) so frame production can't keep up with real time; assert the drop-oldest queue behaves correctly (§4.2 TC-QUEUE-02 semantics hold under real scheduling pressure, not just a scripted test sequence) and the process doesn't crash or deadlock |
| TC-STRESS-02 | GUI under slow render | Artificially slow down `QImage` construction/paint (e.g. a test build with an injected sleep); assert `CONFLATE` prevents backlog buildup and memory growth in the GUI process |
| TC-STRESS-03 | Network interruption and recovery | Disconnect Wi-Fi on the Windows machine for ~10 seconds mid-run, then reconnect; assert the GUI resumes showing live frames afterward (via ZMQ's automatic reconnect) — reported as an observation, not gated pass/fail, since network degradation handling is explicitly out of scope (requirements.md §5) |

## 11. Manual Acceptance Test

This is the one functional bar requirements.md sets explicitly (§1): "a counter visibly
incrementing at 30 fps."

**Procedure:**
1. Start the producer on the Jetson.
2. Start the GUI on the Windows laptop, pointed at the Jetson's current IP.
3. Visually confirm the displayed image shows a legible, incrementing milliseconds-since-1970
   counter.
4. Visually/qualitatively confirm the counter's update rate looks like ~30fps (no requirement for
   frame-accurate measurement here — that's what the telemetry panel's own instrumentation is for;
   this step is a human sanity check, not a precision measurement).
5. Confirm the telemetry panel is simultaneously showing live-updating values (not frozen) for
   queue depth stats, time-in-queue stats, copies/frame, CPU time, RSS, producer µs/frame, and
   latency.

**Pass criterion:** all five steps observed true. This is binary — either the demo works as
described or it doesn't; there is no partial credit or threshold (consistent with requirements.md
§5's explicit non-goal of pass/fail latency thresholds — that non-goal applies to *latency
numbers*, not to this functional smoke test).

## 12. Non-Functional Observations (reported, not gated)

Per requirements.md §1 and §5, these are measured and written up, not pass/failed:

- **End-to-end latency**: `TelemetryStats`/`ImageFrame` timestamp diff, captured over a sustained
  run, reported as observed min/max/mean. Caveat to document alongside the numbers, per
  requirements.md §6 decision #11: both machines' clocks are assumed synchronized, no calibration
  performed — the reported number includes any real clock offset, unmeasured.
  Also note (per this session's discussion): raw Wi-Fi bandwidth for uncompressed YUY2 at
  800×600×30fps (~230 Mbps sustained) is not being separately characterized or treated as a
  concern — reported latency numbers may reflect network throughput limits, and that's accepted
  as within scope of "measure the actual pipeline," not something to work around.
- **CPU/memory overhead**: `getrusage()` values over a sustained run, reported as observed
  ranges.
- **Memory-copy count**: reported as the audited constant (§9), cross-referenced against the
  design's own claim for consistency.

## 13. Known Limitations Affecting Test Interpretation

Carried from design.md §9, relevant to how test results should be read:

- Clock synchronization between Jetson and Windows is assumed, not verified or calibrated —
  latency numbers are only as good as that assumption holds in the test environment.
- The slow-joiner ZMQ behavior (GUI missing frames published before it connects) is intentional
  and expected — not a bug to chase if observed during E2E testing (§8 TC-INT-G-04 exercises the
  adjacent case of GUI-before-producer, which is the scenario that should work cleanly).
- `memory_copies_per_frame` is a static, audited constant, not a live measurement — a code change
  that alters the copy path won't automatically be reflected in this number unless someone re-runs
  the §9 checklist and updates the constant.

## 14. Out of Scope (per requirements.md §5)

Not tested, by design:
- Security/authentication on ZMQ sockets.
- Multi-client fan-out (multiple simultaneous GUI subscribers).
- Dynamic reconfiguration of resolution/frame rate at runtime.
- Service discovery / auto-detection of the producer's IP.
- Zero-copy ZMQ send paths (there's nothing to test — this design deliberately doesn't implement
  them).
- Formal network-degradation handling beyond the observational check in TC-STRESS-03.
- Pass/fail latency thresholds.

## 15. Requirements Traceability

| Requirement (requirements.md §) | Covered by |
|---|---|
| §3.1 Synthetic camera (30fps, 800x600, YUY2, U/V=128, clock text) | TC-CAM-01…08 |
| §3.2 Producer main capture loop | TC-INT-P-01…04, TC-COPY-01…05 |
| §3.3 Telemetry object, thread-safe bounded drop-oldest queue | TC-QUEUE-01…07, TC-TEL-06 |
| §3.3 Running stats, full-run window | TC-STATS-01…03 |
| §3.4 Telemetry content (queue stats, copies, CPU/mem, µs/frame) | TC-TEL-01…05, §9, §12 |
| §3.4 Image message content (payload, format, capture timestamp) | TC-CAM-01, TC-WIRE-02 |
| §3.5 GUI (PyQt, CONFLATE, deserialize, render, 30fps visible) | TC-GUI-01…07, TC-INT-G-01…04, §11 |
| §4 Wire protocol stability | TC-WIRE-01…04 |
| §6 decision #6 Backpressure/drop policy | TC-QUEUE-02, TC-TEL-06, TC-INT-G-02, TC-STRESS-01/02 |
| §6 decision #11 Clock sync assumption | §12, §13 |
| §6 decision #15 Telemetry publish cadence (1:1 with frames) | TC-TEL-02 |
| §1 Copy count / latency measurement goals | §9, §12 |
