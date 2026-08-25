# GUI — Build & Run Notes

PyQt6 client for the v4l2-demo project (design.md §6). Runs on this Windows
laptop; connects to the producer's two ZMQ `PUB` sockets on the Jetson.

> **Not yet run/tested on this machine** — this dev machine currently has no
> Python interpreter installed, so none of this has been executed or
> verified end-to-end here. Treat the code as reviewed-but-unexecuted until
> someone runs it. See "First run" below for what to check once Python is
> available.

## Setup

```powershell
py -3 -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

## Generate protobuf bindings

`telemetry.proto` is the single source of truth for the wire format
(design.md §2) and is not checked in as generated Python — regenerate it
after every fresh checkout or `.proto` change:

```powershell
.\generate_proto.ps1
```

This requires `protoc` on `PATH`, and its version should match the
`protobuf` package pinned in `requirements.txt` (a mismatch is a known
failure mode — test-plan.md TC-WIRE-01). Fill in the actual versions used
here once this has been run for real:

| Component | Version |
|---|---|
| Python | TBD |
| `protoc` | TBD |
| `protobuf` (pip) | TBD |

## Run

```powershell
python gui.py --host <jetson-ip>
```

`--image-port` (default 5555) and `--stats-port` (default 5556) override the
fixed defaults if needed (design.md §5.3).

## Testing without Jetson hardware

`tests/fake_producer.py` publishes synthetic `ImageFrame`/`TelemetryStats`
messages on the same ports/wire format as the real producer (test-plan.md
§8), so the GUI can be exercised end-to-end from this machine alone:

```powershell
python tests\fake_producer.py
# in another terminal:
python gui.py --host 127.0.0.1
```

## Unit tests

```powershell
pytest tests\
```

Covers YUY2→RGB conversion (`test_yuy2.py`, TC-GUI-02/03) and the latency
calculation (`test_latency.py`, TC-GUI-04). `ZmqReceiver`'s threading
behavior (TC-GUI-05/06) and the full end-to-end integration cases
(TC-INT-G-01…04) are not yet covered by automated tests — `pytest-qt` would
be needed for the former; both are candidates for follow-up work rather than
things verified here.

## First run — things to actually check

- [ ] `generate_proto.ps1` succeeds and produces `telemetry_pb2.py`.
- [ ] `pytest tests\` passes.
- [ ] `python gui.py --host 127.0.0.1` against `fake_producer.py` renders a
      visibly incrementing counter and a live-updating telemetry panel
      (the acceptance test in test-plan.md §11).
- [ ] Against the real Jetson producer, confirm the latency figure is a
      plausible small positive number, not wildly off (a large or negative
      value likely means clock sync, not a code bug — requirements.md §6
      decision #11).
