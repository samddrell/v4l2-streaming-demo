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

Produces `build/v4l2_producer` (the producer binary) and `build/producer_tests`
(the GoogleTest suite — see [../docs/test-plan.md](../docs/test-plan.md) §4).

`proto/telemetry.proto` is generated into `build/generated/` at build time and
is not checked in (design.md §8).
