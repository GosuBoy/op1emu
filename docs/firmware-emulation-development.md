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

## Next development milestones

The purpose of this branch is to make op1emu useful for the ordinary
edit-build-test-debug cycle of OP-Doom and other custom firmware.

1. Record reproducible firmware fixtures and run provenance, including the
   emulator revision, image identities, inputs, and initial boot state.
2. Add a coherent codec/clock/SPORT/FIFO/DMA model so hardware-intended audio
   initialization can execute with its checks intact. Cover both successful
   transfer and missing-clock/backpressure behavior.
3. Extend deterministic controls and timestamped native PCM capture for
   gameplay, display, mixer, and streaming regressions.
4. Develop portable guest inspection and debugging, then complete machine
   checkpoints and targeted interrupt/reset qualification.

These are future milestones. This integration does not implement the missing
audio model or incorporate the separate OP-Doom emulator-only checkpoint.
Existing provisioning/decryption and SVG features retain their documented
experimental scope. OTP retrieval remains explicit and opt-in; normal tests
use synthetic inputs and do not fetch OTP or firmware.

## Keeping the branch maintainable

Use this branch as the base for subsequent firmware-emulation work. Preserve
focused commits and regression tests so changes can later be proposed
upstream independently. When updating from upstream, merge the reviewed
upstream revision, preserve any integration work, and rerun the branch's CI.

Record the exact revision used for firmware captures. A passing synthetic CI
run and a successful emulator firmware run establish different things; keep
their results separate when reporting hardware readiness.
