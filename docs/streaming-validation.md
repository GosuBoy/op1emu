# Streaming validation

This page records the 29ba89a/32cce2e checkpoint. The subsequent
[44.1 kHz and guest mixer validation](doom-mixer-validation.md) preserves
these 48 kHz image/PCM/CSV identities and adds a separate rate matrix.

The public fixture executes through the normal loader/CPU/TWI/SPORT/DMA and
SIC/CEC paths. The legacy default, opt-in codec assumptions and unchanged R4
refusal are preserved. These are synthetic digital results, not physical
BF524 deadline or OP-1 analog-output qualification.

## Local result

Apple silicon, Apple Clang17/LLVM15, Release: all 35 CTests passed, including
prior frontend, startup, legacy, transport and DMA coverage. The final
streaming oracle and strengthened FIFO mutation test also passed after their
last diagnostic/test changes. All three public stream images rebuild exactly
with stock GNU Binutils2.44. No C compiler/runtime, private input or game asset
is required.

Every mode runs twice without chaining and once with chaining. PCM, CSV and
complete register/event JSON match within each mode. The normal CLI also
matches normal, delayed and premature native PCM/CSV. The short-budget run
fails with diagnostic state rather than false completion.

| Case | DMA IRQs | Fresh / repeated components | Deliberate corruption | LR phase errors | Guest duration |
|---|---:|---:|---:|---:|---:|
| normal | 256 | 32768 / 0 | 0 | 0 | 345.699550 ms |
| delay | 256 | 32768 / 59 | 0 | 31488 | 346.314133 ms |
| premature | 256 | 32768 / 0 | 120 | 0 | 345.926603 ms |
| pause | 256 | 32768 / 0 | 0 | 0 | 346.199550 ms |
| clock-timeout | 32 | 4138 / 0 | 0 | 0 | 50.967757 ms |

Successful runs complete 256 work units of 64 stereo frames. Each of the two
buffers is produced 128 times (127 reuses). Normal output contains 16384 stereo
frames, 341.333333 ms at 48 kHz, 131072 native bytes. First/last serial completions
are4372575/345695491 ns; their span is341322916 ns. Each component follows the
independently calculated generation/channel/frame formula. Zero samples are
dropped, duplicated or stale in the normal run, and no channel swap occurs.

Every normal completion is SYNC1, remaining 0/controller FIFO0/RUN0/DONE1;
128 source words were read and accepted. At each block-completion point six
SPORT words remain, including at the final interrupt. All256 guest ledger
entries show IVG9 IPEND and the expected saved post-IDLE RETI, IRQ1 before
W1C/0 afterward, followed by main-context release and refill. No interrupt
storm remains. All1283 normal guest records are retained. Expanded prefix
reads, all per-block source/accept digests, codec/TWI and SPORT error evidence
have explicit bounds and zero overflow.

Normal steady-state underflow/overflow counts are zero. One empty-slot
underflow is observed during bounded final drain; no replay completes. The
65 us guard is derived from six outstanding32-bit SPORT words at 3.072 MHz
(62.5 us), with bounded stop overhead before a 72.9167 us completed replay.

Delayed production exposes59 valid repeated last-word components and an odd
LRCLK phase slip affecting31488 subsequent fresh components. Their payload
order remains intact; cadence and flags reveal that concatenated fresh PCM
alone would hide the playback failure. Underflow count 60 includes shutdown.
This is finite restart behavior; it does not predict the stale-memory behavior
of an unqualified cyclic DMA mode.

Premature reuse preserves the first eight captured source words and corrupts
exactly the remaining 120 of block 0. Controller unit tests independently
establish this4-peripheral+4-DMA prefetch boundary and safe mutation after the
last source read, including with four words still pending in DMA under SYNC0.

Pause/resume adds exactly500000 ns to the applicable serial timestamps while
retaining values/phase and avoiding a false watchdog. Passive separated stall
snapshots agree: controller FIFO4, SPORT FIFO4 plus holding/shifting, ten total
outstanding words; source read 52/accepted 48/remaining 76. No buffering or
underrun accrues from the stopped interval. Without restoration, the guest 4 ms
watchdog returns WDOG with 32 completions and 4138 serialized words, preserving
partial-work diagnostics. Removing MCLK under active power is an explicit
digital negative stimulus, not an analog shutdown sequence.

## Current image and capture identities

Each image is2308 bytes; source/tool/linker/global-symbol identities are in
`tests/fixtures/stream_guest/manifest.json`. `pause` and `clock-timeout` use
exactly the normal image. SHA256:

- `delay.ldr`: `b487a90f1bb6e777e8a49fdc6463c4b5f48fa4a439ef2e1bcad1bc676169dcc5`
- `normal.ldr`: `e84835c648249162dbf9b72d532214bd65c6567071a42c4ccd70ad1e1bd53075`
- `premature.ldr`: `6330b3e3f070b9e16a10e33e710a97cf79fa7d83dfd54d517d0b9bedb23bd93a`

Generated evidence identities (`stream-guest/identities.json` in CI):

### normal

- `pcm_sha256`: `fc7e7b88a9eac4c0afa054a9c3b33a67d0fc9d58563ef35c985cffe42a7983b2`
- `csv_sha256`: `88e75fd4e16e06d35ce68f49c77b08b17298a99816980e114964ab83a7b08b5d`
- `events_sha256`: `d660f764e9fbfe9a5b34720e87ee9206d779499de101b1a4222f5126ee8688ce`

### delay

- `pcm_sha256`: `19194a36509e866f9a7d1e391d2791693637fbca4ca4060e881b253646a0f6c0`
- `csv_sha256`: `190ae9a888edf5c34171889ac42f35569bd7544decba6dd71c6f5f938e2d8b60`
- `events_sha256`: `c0e11e55eeddfbb74c11907ca993db7f9a1bc8309837d87ab22e54f99a13aad0`

### premature

- `pcm_sha256`: `89905cab6ff19c35939c9f2e77969ead1ec930c5745151db5330e97411b3d649`
- `csv_sha256`: `27a41b1c1f9c8af0f8ef8d6072432bddac28a9883d901d533aeead2783481a3b`
- `events_sha256`: `f6687c59ef03191a6fad2e6e1efacf2cec69b5637eabf97836a3597b218e5299`

### pause

- `pcm_sha256`: `fc7e7b88a9eac4c0afa054a9c3b33a67d0fc9d58563ef35c985cffe42a7983b2`
- `csv_sha256`: `a93b50c4bea1cbc4e7e1a17afa1c50fc2aaf79c1c079772613558c7d1f2f8b18`
- `events_sha256`: `3dd91889639a11a7a637e67191b6be0e51370a5b3f50090c7cc73490922bdb8d`

### clock-timeout

- `pcm_sha256`: `576de85b3e8080fc7003212740444a1dfd3f5c33593c76e427e49f13c0731b73`
- `csv_sha256`: `48f32d4a48b34cd9ceeda074fd1b4aa07477526591e7a9092d9e06b7432ec3e9`
- `events_sha256`: `e0f62bd2846213f9c25cf930aa62a1d3e0f07a36cf17ffedd4028c3d6a666743`

## Earlier finite-test correction

The finite32 DMA controller FIFO/SYNC correction intentionally changes source
memory counts and completion timing in the preceding fixtures. Their new
images are:

- `sport_guest` (868 bytes): `ddaa636d2deb5cb91c1a05173bea3fa111e74a6e62984d6c781cd15f09b02789`
- `codec_guest` (1700 bytes): `019020eacf382b84933557242621d9a23d0ce7ac2617e44cd244117833a176d1`

Cold clockless source count/address now includes four controller-prefetched
words. SYNC0 interrupt now arrives with six serialized words, four DMA words
and six SPORT words outstanding. Guards become321 us at 1 MHz and 105 us at
3.072 MHz. The source/count/IRQ/event identities consequently change; historical
records retain the original commit-bound evidence. Existing finite PCM/CSV
remain byte-identical to the preceding milestone. Private R4 was not changed
or rerun under another admission/profile assumption.

## CI

Implementation:
[`29ba89adfaedfe91ad3315e0b581ddbbd0a43acf`](https://github.com/GosuBoy/op1emu/commit/29ba89adfaedfe91ad3315e0b581ddbbd0a43acf).
[CI run 40872373956](https://github.com/GosuBoy/op1emu/actions/runs/40872373956)
completed successfully on 2026-10-29. All six jobs passed:

- Linux without profiling: 35 CTests, frontend/OpenGL and CLI checks.
- Linux with profiling: 38 CTests, frontend/OpenGL and CLI checks.
- Apple silicon macOS15: 35 CTests, frontend and CLI checks.
- Stock GNU Binutils2.44 rebuild: both corrected finite images and all three
  streaming images, complete source hashes and symbol manifests match.
- Python tooling: 15 unit tests and 183 pytest tests.
- libc++ TimeSource build/test.

Downloaded public artifact evidence:

| Configuration | Artifact ID | ZIP SHA256 |
|---|---|---|
| Linux, profiling OFF | 12895964763 | `5cd5f87ed74d67d7045b7d0fba03ffb668069ba170636ca7408b6c9ed7ff1545` |
| Linux, profiling ON | 12897005861 | `19a5df3d5fb7ab7fbfcac2fb08cb5541541ac25ed2896cf0ce5c8ed12979f256` |
| macOS, profiling OFF | 12897080640 | `ef231eab58d184ff9c91c7f86cc08d58ad31d5550bc5a410e4aa2a56db0e1056` |

All 78 new streaming capture/event/driver-log files are byte-identical across
these three CI configurations and the local run. The 104 current finite
transport/codec capture/event/driver-log files also match across configurations.
All 60 prior PCM/CSV files match the preceding
[codec milestone's CI](https://github.com/GosuBoy/op1emu/actions/runs/40860133252).
The finite state/event/log differences are the explicitly documented DMA
correction, not a preserved incorrect memory-count expectation.

Comparisons exclude normal CLI console logs, which contain build/platform
details; they include CLI native PCM/CSV. ZIP container identities differ due
to metadata and console logs. All artifacts contain only public synthetic
inputs/captures; no private firmware, sources, provenance or traces are uploaded.

See [contract, reproduction, source findings and limits](streaming-audio.md)
and the [fixture interface](../tests/fixtures/stream_guest/README.md). Exact
private source/toolchain/build dependency details remain outside public
commits and artifacts. No physical device access or flashing was performed.
