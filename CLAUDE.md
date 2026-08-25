# v4l2-demo — Project Context

## What this is

A prototype/demo measuring the cost of carrying video frames over ZeroMQ as Protobuf messages.
Core question: how many memory copies happen and what's the minimum achievable latency,
end to end.

Two components:
- **Producer** (C++) — runs on a **Jetson Orin Nano** (JetPack 6 / L4T R36.4.3, Ubuntu 22.04,
  aarch64). Owns a synthetic (non-kernel) V4L2-like camera class and a telemetry object, wraps
  frames in protobuf, publishes over ZeroMQ.
- **GUI** (Python/PyQt6) — runs on **this Windows laptop**, on the same Wi-Fi network as the
  Jetson. Subscribes over ZMQ, renders the image stream and telemetry live.

## Current status

Docs-only so far — **no code has been written yet**. `docs/` contains:

- `human-requirements.md` — the original requirements, written by the user, verbatim. **Do not
  edit this file** — it's the source-of-truth record of original intent.
- `requirements.md` — an expanded/clarified version of the above, with ambiguities resolved
  through Q&A with the user (see "Key decisions" below) and a full "Resolved Decisions" section.
- `design.md` — concrete engineering design built from `requirements.md`: protobuf schemas,
  class interfaces (`SyntheticV4L2Camera`, `Telemetry`), threading model, copy-count enumeration,
  toolchain choices, repo layout.

**Not yet written**: `test-plan.md`. That's the next doc to produce, then actual implementation
(producer C++ code, GUI Python code).

This `docs/` folder was copied here from a WSL/Ubuntu working copy where the requirements and
design docs were originally drafted with Claude. This Windows folder is now the primary place to
continue the project (the user doesn't need to develop on the WSL VM — the real producer target
is the Jetson, and the GUI target is this Windows machine).

## Key decisions already made (see requirements.md §6 for full detail/rationale)

- Pixel format: `YUY2` (packed YUV 4:2:2), U/V bytes pinned to 128 for monochrome — not a
  single-plane grayscale format, deliberately.
- Telemetry stats are published as a **protobuf message**, not free text (deviates from the
  wording in the original human-requirements.md, which said "text string").
- Copy-count instrumentation: phase 1 counts userspace copies only (4, per design.md's
  enumeration) and is reported as a **documented constant from code audit**, not measured live
  at runtime — a known simplification, open to revisiting if runtime instrumentation is wanted.
- Latency is computed from a **metadata timestamp field** on the image message (GUI receipt time
  minus Jetson capture time), never by reading the rendered clock pixels. Both machines' clocks
  are **assumed synchronized** for now (no calibration implemented) — documented limitation.
- Bounded queue (telemetry object, producer side): 30 frames (~1s at 30fps), **drop-oldest**
  when full. GUI mirrors this with ZMQ's `CONFLATE` option on the image subscriber socket.
- Telemetry stats publish at the same 30Hz rate as the image stream.
- Endpoint config: producer binds fixed ports (5555 images, 5556 telemetry) on all interfaces;
  GUI is given the Jetson's IP manually via CLI flag — no service discovery (explicit non-goal).
- Explicit non-goals: real V4L2 kernel driver (phase 2/bonus only), security/auth on ZMQ, multi
  -client fan-out, zero-copy ZMQ send APIs, network degradation handling, pass/fail latency
  thresholds (this is pure latency *reporting*, not a gated test).

## Working with the user

- The user is doing hands-on embedded/systems work (Jetson, V4L2, ZeroMQ, protobuf) — comfortable
  with low-level C++ and Linux systems concepts; explanations don't need to be dumbed down.
- Prefers being asked directly about open/ambiguous requirements rather than having assumptions
  made silently — walk through open questions explicitly and let them decide, one at a time is
  fine.
- Wants original source documents (`human-requirements.md`) preserved untouched; new
  interpretations/expansions go in separate derived docs.
