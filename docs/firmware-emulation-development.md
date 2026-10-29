# Independent firmware development branch

Work on `GosuBoy/op1emu:development/firmware-emulation` to integrate and test
firmware-development improvements before proposing focused changes upstream.
The initial baseline is upstream commit
`d4a4146281a815ccf697691325d42e25d7c32e85`.

## Included contributions

These are the exact feature heads incorporated by the initial integration.
The merge commits preserve their original history and dependencies.

| Contribution | Pull requests | Integrated head |
|---|---|---|
| Stock firmware display DMA regression | [upstream #73](https://github.com/op1emu/op1emu/pull/73), [fork #2](https://github.com/GosuBoy/op1emu/pull/2) | `5d7e154b8c5af9dad92d5b197d9b29ffb07cab03` |
| PG3 interrupt acknowledgment fix and regression | [upstream #75](https://github.com/op1emu/op1emu/pull/75), [fork #4](https://github.com/GosuBoy/op1emu/pull/4) | `b2b8f651a5837b5c0c2d8e94a93d48aa45ba0bd6` |
| NAND content provisioning | [upstream #76](https://github.com/op1emu/op1emu/pull/76), [fork #3](https://github.com/GosuBoy/op1emu/pull/3) | `9cad70cc2dd86ceb80997885e5b0cf33e61ca530` |
| Optional OTP retrieval and local firmware decryption | [fork #5](https://github.com/GosuBoy/op1emu/pull/5) | `d17544343bc7d633a06f42a6319f5654db89ffc1` |
| SVG/resvg frontend and floating-window support | [upstream #77](https://github.com/op1emu/op1emu/pull/77), [fork #6](https://github.com/GosuBoy/op1emu/pull/6) | `5b51152764666c466348217d2f60a27853910317` |

The OTP head includes the NAND provisioner, and the SVG head includes the
floating-window experiment. Each change is incorporated once. The initial
upstream baseline already contains the LDR truncation fix (#58) and NAND
page-column bounds fix (#69).

Submodule pins at integration:

- `ext/bcore`: `f898ced95de79538c7fbaf0e87270f93fe4c77f4`
- `ext/bfin_sim`: `08e915326ae7efbccd889a11021e6e216e80eca7`

## Checkout and validation

```sh
git clone --branch development/firmware-emulation --recurse-submodules \
  https://github.com/GosuBoy/op1emu.git
cd op1emu
python3 -m unittest discover -s tools -p 'test_*.py' -v
python3 -m pytest tools/tests -q
```

Use the platform dependencies and SVG setup described in the main README.
For a native build with all firmware-free regression targets enabled:

```sh
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release \
  -DOP1_BUILD_HEADLESS_TESTS=ON \
  -DOP1_BUILD_DMA_TESTS=ON \
  -DOP1_BUILD_UI_TESTS=ON \
  -DOP1_ENABLE_SVG=ON \
  -DENABLE_PROFILING=OFF
cmake --build build
ctest --test-dir build --output-on-failure
./build/op1emu --help
```

Supply `LLVM_DIR` if LLVM 15 is not found automatically. SVG builds require
Rust; CI uses Rust 1.91.1 and pinned resvg 0.49.0. Use
`-DOP1_ENABLE_SVG=OFF` for the existing PNG frontend without Rust. Profiling
currently requires Linux; it remains disabled in the macOS configuration.

The branch's CI runs on code pushes directly, without requiring an upstream
PR. It covers Linux with SVG enabled, Linux with profiling enabled and SVG
disabled, macOS arm64 with SVG enabled, Python tooling, and the libc++ time
test. Linux also exercises the OpenGL shell through Xvfb. Hosted macOS runs
the OpenGL tests when a context is available and reports when it is not.

The display DMA and GPIO tests must remain registered alongside the existing
headless, device, parser, and UI regressions. CI uses synthetic inputs; it
does not qualify a proprietary firmware image or a physical OP-1.

## SPORT transmit milestone

The first transport milestone adds an explicit `--sport-tx-mode transport`
option with shared PIO/DMA buffering, guest-time clock/FS-driven serialization,
backpressure, status/interrupts, and native PCM plus timestamped capture.
`legacy` remains the default for compatibility with established firmware runs.

See [the behavioral contract and reproducible headless demonstrations](sport-transmit.md)
for sources, model assumptions, supported registers, exact stimulus/output
identities and the remaining hardware-validation gaps. See the
[validation record](sport-transmit-validation.md) for revision and test results.
The transport and two capture demonstrations are registered with CTest alongside
the existing integration regressions. CI uploads firmware-free capture evidence on all
three native build configurations.

The available custom-firmware sources establish SPORT0/DMA4 selection, but a
hardware-intended OP-Doom image could not be executed without its separately
preserved private build/boot inputs. Codec identity/clock routing and physical
output remain unverified. No proprietary inputs are added to this repository.

## Next development milestones

1. Restore the pinned OP-Doom fixture privately with exact image/boot/compiler
   provenance; establish codec identity and board clock routing; run unchanged
   initialization and compare clock/FS/DMA observations with hardware.
2. Qualify DMA prefetch/SYNC completion, descriptor timing and codec controls
   needed by that fixture; extend the bounded transport only where evidenced.
3. Extend deterministic controls and native capture for gameplay, display,
   mixer and streaming regressions, keeping host playback separate.
4. Develop portable guest inspection/debugging, then machine checkpoints and
   targeted interrupt/reset qualification.

## Keeping the branch maintainable

Use this branch as the base for subsequent firmware-emulation work. Preserve
focused commits and regression tests so changes can later be proposed
upstream independently. When updating from upstream, merge the reviewed
upstream revision, preserve any integration work, and rerun the branch's CI.

Record the exact revision used for firmware captures. A passing synthetic CI
run and a successful emulator firmware run establish different things; keep
their results separate when reporting hardware readiness.
