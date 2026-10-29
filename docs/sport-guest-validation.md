# Guest-executed audio validation

Date: 2026-10-29. Continuation of transport implementation
`ef6d3056c3fe440a2a50ac64164f62ea6d934270` and validation
`bb863b2ec29dd92338e78ac6b12d3c886795c033` on
`development/firmware-emulation`. The branch was fetched and already current;
no newer work was replaced. Main, PR branches and both submodule pins remain
unchanged. The `legacy` audio default remains unchanged.

Guest implementation:
[`33dc8a77324246cdeee769e074d916df49c66776`](https://github.com/GosuBoy/op1emu/commit/33dc8a77324246cdeee769e074d916df49c66776).

## Reproduction and fixture identity

Follow the [development build commands](firmware-emulation-development.md), then:

```sh
ctest --test-dir build -R '^op1-sport-guest$' --output-on-failure
python3 tests/sport_guest_integration.py ./build/op1-sport-guest-test \
  ./build/sport-guest ./build/op1emu
```

The [fixture README](../tests/fixtures/sport_guest/README.md) contains the
complete assembly/linker/tool build commands, assumptions, register sequence,
mailbox ABI and failure limits. No C compiler/runtime is involved: GNU
Binutils **2.44**, target **bfin-elf**, assembler `-mcpu=bf524`, link at
`0xffa00100`. The 868-byte checksummed LDR SHA256 is:

`0aca5665cdf84c6bcdffb972d568b868d5706cada7d9ead8289576e422ed9e60`

The source hashes and exported addresses are checked in
`tests/fixtures/sport_guest/manifest.json`. A local fresh build of the
assembler/linker from the unmodified GNU release archive, using system zlib,
reproduced the image, source hashes and symbols exactly (`build.py --check`).
CI independently performs that rebuild on Linux from the pinned source URL
and archive SHA256, alongside native execution on all three configurations.

## Executed guest evidence

The only host test stimulus is the explicit synthetic external clock/FS:
**1 MHz TSCLK, 64 clocks per stereo frame**, or no clock/FS at all. The image
is identical in both runs. All sample stores, SPORT/DMA/interrupt/timer
configuration, IDLE, W1C acknowledgment and return are guest instructions.
The test and frontend share `LDRParser`/`LoadDXE` and the normal `cpu.Run()`
path. This is direct supported LDR execution, not NAND boot-ROM emulation.

| Observation | Clocked result |
|---|---|
| Guest initialization | Exact sixteen samples at `0x3000`, `0x12340001 + i*0x101`; configured SPORT0/DMA4/SIC17/IVG9 readback verified. |
| DMA acceptance | All 16 accepted; guest ISR observes count=0, current address=`0x3040`, DONE=1. Only 10 words have serialized at ISR entry; six remain buffered. |
| Interrupt delivery | Exactly one guest DMA ISR at `0xffa00368`; ISR-entry CYCLES=129116 (322.790 us). IPEND=`0x8210` includes IVG9 and background IVG15. Saved RETI=`0xffa00294`, the instruction following guest IDLE. |
| Guest acknowledgment and return | DONE 1→0 by guest W1C, recorded at CYCLES=129152; guest ISR tag=`0x444d4139`, main after-RTI tag=`0x52544939`; R5 canary preserved. |
| Transport drain | Last new word completes at **513790 ns**, after DMA acknowledgment. All 16 exact ordered words captured, alternating slots, no invalid or repeated captured words. |
| Later interrupt check | Guest spends another 80000 timer cycles draining; DMA ISR entry count stays one. Timer ISR returns from the drain IDLE (`RETI=0xffa002d0`). Final DMA IRQ=0, CEC ILAT=0 and IPEND=`0x8000`. |
| Guest completion | `PASS` signature, 992 retired packets, 208257 IDLE cycles, 54 guest IDLE entries, 209249 total cycles. |

DMA acceptance is observed by the guest ISR, not assigned the exact time of
the hardware DONE assertion. CYCLES values are the CPU model's basic-block
entry timestamps; they are not pipeline/ISR latency measurements. Serial
completion timestamps are modeled clock-edge times. Those qualifications do
not affect the demonstrated ordering: DMA acceptance → guest ISR/acknowledgment
→ six remaining serial completions → guest completion.

At the final new word the model can start an underrun replay and set TUVF.
The guest's fixed 200 us drain guard disables SPORT before that replay can
complete. One underflow and pre-disable STAT=`0x50` are expected; post-disable
STAT=`0x40`, FIFO/holding/shifter empty. This is a fixture-specific stop window,
not a general drain protocol or a hardware underrun qualification.

Without external clock/FS, guest execution reaches the same SPORT/DMA setup:
accepted=4, FIFO=4, holding/shifting=false, DMA count=12/address=`0x3010`,
STAT=`0x48`, transmitted=0. There is no DMA ISR/tag/after-RTI marker. The guest
core timer interrupts its IDLE once and main publishes `STAL`; 1286 packets,
799063 IDLE cycles, 800349 total cycles (about 2 ms), 82 IDLE entries. No FIFO
overflow or invented clockless underflow occurs. Both PCM and serial body
are empty; the transport remains enabled and stalled for final inspection.

## Repeatability and output identities

For each mode, two unchained runs and one chained run produce byte-identical
PCM, CSV and event JSON, despite different host Run counts (clocked 166 vs
70, clockless 219 vs 94). The shipping `op1emu NAND LDR` path produces
identical SPORT0 PCM/CSV in both modes; SPORT1 emits nothing. The CLI uses a
temporary missing NAND in snapshot mode and temporary synthetic OTP/input
script. No user/private NAND or OTP is accessed.

| Output | Bytes/meaning | SHA256 |
|---|---|---|
| `clock-0.pcm` | 64 bytes, 16 little-endian u32 native samples | `9e412b21be559d5c47c41f35835d565a31a05dc458747b28d251c39ad2fa7f44` |
| `clock-0.csv` | Header + 16 serialized words, 33790..513790 ns, 32000 ns spacing | `5b40031ec99504c17682f19e16f31c1d7fb69514abb1e92ef48aed759639e24e` |
| `clock-0.json` | Guest event/mailbox and final device state | `ea26c22b2ca8cb3012d958ea31efcc160e0107800803c08cd2b4289598da5872` |
| `no-clock-0.pcm` | Empty | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `no-clock-0.csv` | Header only | `a5c670d61d4cee822b3a6e8db7a42bf4058fb634f0855f08a1c8e555003fed23` |
| `no-clock-0.json` | Watchdog/stall event record and bounded device state | `ed22429958b8b0b7b709d4bb881991b861961dd9ece65ee973a72c14fa993905` |

`build/sport-guest/identities.json` records these hashes. Native capture has
no resampling, silence insertion, host audio conversion or sound-device use.
CLI logs contain host wall times and temporary paths and are not repeatability
oracles. Guest JSON deliberately excludes host Run count and wall time.

## Validation

Local host: macOS arm64, AppleClang 17.0.0.17000404, LLVM 15, Release,
SVG on, profiling off. A clean rebuilt `op1emu --build-info` reports host
`33dc8a773242`, `host_dirty=false`, bcore `f898ced95de7`, `bcore_dirty=false`.

- All **28/28 CTests passed**, 16.79 s, including the new guest integration
  and all prior transport, frontend, provisioning-related loader/NAND, legacy
  audio, display DMA, GPIO, descriptor, CPU and device regressions.
- **15/15 Python tooling unittests passed**. Linux-specific profiling pytest
  coverage remains assigned to Linux CI, as in the previous milestone.
- Local OpenGL shell and floating-window regressions passed.
- Fresh stock GNU source build reproduced the LDR and manifest exactly.
- The deliberately limited 100-cycle run failed as expected and emitted PC,
  exception/CEC/SIC state, DMA address/count, SPORT status/buffering/counters
  and guest mailbox/sample diagnostics. Normal runs are bounded by both
  guest watchdog/guard and outer cycle/Run/wall/process limits.

## CI and independent capture comparison

[Implementation workflow](https://github.com/GosuBoy/op1emu/actions/runs/40829108184)
at the exact implementation revision: **all six jobs passed**.

| Configuration | Result |
|---|---|
| Linux, SVG on, profiling off | **28/28 CTests**, 44.83 s; CLI, native captures and Xvfb OpenGL regressions passed. |
| Linux, SVG off, profiling on | **31/31 CTests**, 64.79 s; CLI, native captures and Xvfb OpenGL regressions passed. |
| macOS arm64, SVG on, profiling off | **28/28 CTests**, 42.96 s; CLI and native captures passed. Hosted GL context unavailable (probe 77); equivalent GL checks passed locally and on Linux. |
| GNU Blackfin fixture rebuild | GNU release archive checksum verified; fresh assembler/linker reproduced all 868 LDR bytes, source hashes and exported symbol addresses. |
| Python tooling on Linux | **15/15 unittest, 183/183 pytest passed**. |
| libc++ | TimeSource regression passed. |

Downloaded all three native artifacts and compared **30 PCM/CSV/JSON files
from each** byte for byte with local macOS: all matched. This includes the
six guest runs, both CLI clock conditions, output identities and the
deliberately limited failure dump. Host logs are retained for diagnostics and
excluded from the byte comparison because they contain wall times/paths.

- [Linux profiling-off evidence](https://github.com/GosuBoy/op1emu/actions/runs/40829108184/artifacts/12877748446)
- [Linux profiling-on evidence](https://github.com/GosuBoy/op1emu/actions/runs/40829108184/artifacts/12877833384)
- [macOS evidence](https://github.com/GosuBoy/op1emu/actions/runs/40829108184/artifacts/12878271353)

The existing host-driven transport demonstrations remain in those artifacts
beside `sport-guest/`. The source rebuild job uses public GNU inputs only;
none of the CI jobs accesses the recovered private integration materials.

## Private integration inputs and remaining gaps

A parallel read-only recovery located the exact hardware-intended **R4 route
candidate finite-audio diagnostic**, reference ELF/bootstrap, matching source
checkpoint, compiler/linker pins, stock boot/NAND inputs and content tools.
All 86 package SHA256 manifest entries matched; 78 source/build/header/linker
files matched the recorded private Git revision byte for byte. The actual
plaintext MAIN LDR was identified separately from the reference diagnostic
ELF; the ELF is not a standalone stock-initialized boot image.

Detailed private paths, image/source/tool hashes, loader/patching context,
required boot inputs and a concrete normal provisioning/LDR launch recipe
are recorded in an external private provenance report. No private artifact,
source, NAND, OTP, manifest or launch-specific path is committed here. The
recipe supplies **no forced CLI clock** and remains unexecuted.

Precise outstanding inputs/qualification:

- Exact pinned Linux GCC 13.2.0/binutils executables (or a separately verified
  equivalent rebuild) are needed for a byte-exact R4 rebuild. The native
  macOS compiler's version matches but its binary identity differs.
- The complete R3→R4 updater/container generation and patch/audit workspace
  is absent from the small handoff. Recovered executable bytes are available;
  a complete updater rebuild is not established.
- The recovered V74 gameplay image has audio disabled. R4 is a separate
  finite-tone route diagnostic; no audible OP-Doom gameplay image is identified.
- Codec identity/state, reset/PLL behavior and board pin/clock routing remain
  unmodeled/unqualified. Private hardware-intended initialization and physical
  OP-1 output have not been run or inferred from this synthetic result.

The next milestone connects evidenced codec and board-routing state to this
transport, then runs hardware-intended initialization without forced clocks.
