# SPORT transport validation record

This records the first, host-driven MMIO milestone. The subsequent
[guest-executed milestone](sport-guest-validation.md) adds CPU/IDLE/ISR evidence
and updates the status of private integration inputs; the results below
remain tied to the original revision.

Validation date: 2026-10-29. Implementation:
[`ef6d3056c3fe440a2a50ac64164f62ea6d934270`](https://github.com/GosuBoy/op1emu/commit/ef6d3056c3fe440a2a50ac64164f62ea6d934270),
based on integration checkpoint `a2c9bfd098e3937ce0c6742aa526fb77b198898f`.
The branch was fetched again before committing; no newer work was present.
The pinned bcore and bfin_sim submodules were unchanged.

The [contract, source qualifications and reproduction commands](sport-transmit.md)
are part of the implementation commit. The fixture is synthetic MMIO, not
hardware-intended firmware or a codec initialization replacement.

## Local results

Host: macOS arm64; Release, AppleClang 17.0.0.17000404; LLVM 15; SVG enabled;
profiling disabled. A rebuilt `op1emu --build-info` reported host revision
`ef6d3056c3fe`, `host_dirty=false`, bcore `f898ced95de7`, `bcore_dirty=false`.

- `ctest --test-dir build -j4 --output-on-failure`: **27/27 passed**, 17.26 s.
  Includes existing display DMA, GPIO PG3, NAND/ECC, device, loader,
  descriptor, CPU, legacy audio, SVG and layout regressions, plus the transport
  groups and two native capture demonstrations. Local socket access was
  required for the existing USB/IP test.
- `python3 -m unittest discover -s tools -p 'test_*.py' -v`: **15/15 passed**.
- `python -m pytest tools/tests -q` in an isolated temporary environment:
  **125 passed, 56 failed, 2 errors** on macOS. Existing Linux profiling code
  assumes APIs such as `os.sched_getaffinity` and Linux perf/machine inputs.
  These were not changed as part of the transport work; Linux CI separately
  passed all **183** tests.
- Local OpenGL shell and floating-window regressions: **passed**, including
  SVG rendering/input, alpha, drag, resize and recovery checks.
- UndefinedBehaviorSanitizer with recovery disabled: **six transport groups
  passed**. The fixture and SPORT transport/DMA sources were instrumented,
  linked to the remaining normal Release emulator library. This is not a
  claim that the entire emulator was sanitizer-instrumented.
- AddressSanitizer: **no test result**. This host's runtime deadlocked in
  sanitizer shadow-memory initialization before `main` (confirmed by a
  process stack sample); the processes were stopped. This was not counted
  as a pass.

## Capture evidence

At the exact implementation revision:

```sh
./build/op1-sport-transport-test --demo /tmp/sport-running > /tmp/sport-running.log
./build/op1-sport-transport-test --demo /tmp/sport-clockless --no-clock > /tmp/sport-clockless.log
./build/op1-sport-transport-test --demo /tmp/sport-repeat > /tmp/sport-repeat.log
cmp /tmp/sport-running.pcm /tmp/sport-repeat.pcm
cmp /tmp/sport-running.csv /tmp/sport-repeat.csv
```

Both comparisons passed. The running fixture accepted and serialized 16
32-bit words; its PCM is 64 bytes. SHA256:
`9e412b21be559d5c47c41f35835d565a31a05dc458747b28d251c39ad2fa7f44`.
CSV SHA256:
`3cb8b40c355b3d93712c04a9bcd5061b3a47b49cb4684d215b9c18951ac4ad9e`.
No completed words have timestamps inside the 200..400 us clockless interval.
The separate missing-clock run accepted four words into the FIFO, left 12
words on DMA4, and wrote zero PCM bytes. Its empty-file SHA256 is
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.

## CI

[Implementation workflow run](https://github.com/GosuBoy/op1emu/actions/runs/40824667788): **all five jobs passed** at the exact implementation revision.

| CI configuration | Result |
|---|---|
| Linux, SVG on, profiling off | **27/27 CTests passed**, 56.94 s; CLI, captures and Xvfb OpenGL regressions passed. |
| Linux, SVG off, profiling on | **30/30 CTests passed**, 55.28 s; CLI, captures and Xvfb OpenGL regressions passed. |
| Python on Linux | **15/15 unittest and 183/183 pytest passed**. |
| libc++ | TimeSource test passed. |
| macOS arm64, SVG on, profiling off | **27/27 CTests passed**, 37.17 s; CLI and native captures passed. Hosted OpenGL context unavailable (probe 77); equivalent checks passed locally and on Linux. |

The uploaded Linux and hosted macOS captures were downloaded and independently
compared: both running PCM and CSV files match the local macOS hashes exactly;
both clockless PCM files are empty and their CSV files are identical.
[Linux evidence artifact](https://github.com/GosuBoy/op1emu/actions/runs/40824667788/artifacts/12875639793),
[macOS evidence artifact](https://github.com/GosuBoy/op1emu/actions/runs/40824667788/artifacts/12876435851).

## Claim boundaries

| Claim | Outcome |
|---|---|
| DMA acceptance/backpressure | Demonstrated through real MMIO/DMA paths in the bounded model. Completion is distinct from drain. |
| Serialized native PCM | Demonstrated for specified synthetic clock/FS schedules, including restart, PIO equivalence, striding, packing, underruns and repeatability. |
| Codec initialization | Not validated; I2C codec endpoints remain dummy peripherals. |
| Hardware-intended OP-Doom initialization | Not executed. Source was accessible, but the exact runnable image and its private stock/loader/compiler/build context were unavailable locally. |
| Physical OP-1 behavior | Not tested or inferred from passing emulator tests. No device was accessed or flashed. |

The next milestone is the pinned private firmware fixture plus evidence for
codec identity and the physical clock/pin route, followed by unchanged startup
and hardware trace comparison. A passing transport fixture does not prove a
cause for the reported hardware DMA stall.
