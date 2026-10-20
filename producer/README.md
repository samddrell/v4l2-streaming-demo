# Producer — Build Notes

Toolchain versions actually used for this build, on the Jetson Orin Nano
target (JetPack 6 / L4T R36.4.3, Ubuntu 22.04.5, aarch64), per
[../docs/design.md](../docs/design.md) §2's reproducibility requirement:

| Component | Version |
|---|---|
| GCC | 11.4.0 (Ubuntu 11.4.0-1ubuntu1~22.04) |
| CMake | 3.22.1 |
| `protobuf-compiler` / `libprotobuf-dev` (apt) | 3.12.4-1ubuntu7.22.04.6 |
| `libzmq3-dev` (apt) | 4.3.4-2 |
| cppzmq (FetchContent) | v4.10.0 |
| GoogleTest (FetchContent) | v1.14.0 |

## Build

```sh
sudo apt-get install -y protobuf-compiler libprotobuf-dev libzmq3-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
```

## Alternate target: WSL2 (Ubuntu 22.04.5, x86_64)

The same source and `CMakeLists.txt` build unchanged under WSL2 using the
same apt packages and commands as above. Clone into WSL's own filesystem
(e.g. `~/`), not `/mnt/c/...` — it is much faster and avoids sharing one
`build/` directory between architectures.

| Component | Version |
|---|---|
| GCC | TBD (`gcc --version`) |
| CMake | TBD (`cmake --version`) |
| `protobuf-compiler` / `libprotobuf-dev` (apt) | TBD (`dpkg -l libprotobuf-dev`) |
| `libzmq3-dev` (apt) | TBD (`dpkg -l libzmq3-dev`) |

Fill these in from the first real WSL build; they are expected to match the
Jetson table but apt patch versions can differ.

To connect the Windows GUI to the WSL producer, run it with
`python gui.py --host 127.0.0.1`. WSL2's default NAT mode forwards Windows
`localhost` to services listening in the VM, so no `.wslconfig` change should
be needed. If that proves unreliable (e.g. something on Windows already holds
port 5555/5556, or a VPN/Docker interferes), set `networkingMode=mirrored` in
`%UserProfile%\.wslconfig` (Windows 11 22H2+) and run `wsl --shutdown`.
Don't use the VM's `172.x` address; it changes on restart.

Notes:

- Latency over this path is Windows↔WSL loopback, not Wi-Fi, so it is not
  comparable to Jetson-over-Wi-Fi numbers. Host and VM share a time source,
  but WSL's clock can drift after Windows sleep/hibernate (`date` vs Windows;
  `sudo hwclock -s` or `wsl --shutdown` fixes it).
- `max_rss_kb` and timing-based unit tests may differ from the Jetson; the
  pacing and queue-overload tests are the likeliest to be sensitive.
- The GUI's `gui/telemetry_pb2.py` is generated on Windows (it is not
  checked in). Do not generate it with WSL's protoc 3.12.4.

## Build outputs

Produces `build/v4l2_producer` (the producer binary) and `build/producer_tests`
(the GoogleTest suite — see [../docs/test-plan.md](../docs/test-plan.md) §4).

`proto/telemetry.proto` is generated into `build/generated/` at build time and
is not checked in (design.md §8).
