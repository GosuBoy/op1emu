# Startup investigation checkpoint

Implementation: [`625fd08213b9b583a8176dfb348f8617eced9b33`](https://github.com/GosuBoy/op1emu/commit/625fd08213b9b583a8176dfb348f8617eced9b33).
Validation date: 2026-10-29.
[CI run 40837962821](https://github.com/GosuBoy/op1emu/actions/runs/40837962821)
completed successfully at that exact code revision. All six jobs passed.

| Configuration | Result |
|---|---|
| Linux, profiling OFF, SVG ON | 30/30 CTests, CLI, both GL shell checks |
| Linux, profiling ON, SVG OFF | 33/33 CTests, CLI, both GL shell checks |
| macOS arm64, profiling OFF, SVG ON | 30/30 CTests and CLI; hosted GL context unavailable |
| Python tooling on Linux | 15 unittest tests and 183 pytest tests |
| libc++ | TimeSource regression passed |
| Public Blackfin fixture rebuild | Pinned GNU Binutils build and exact image/source/symbol verification passed |

The new OLED bus and startup-diagnostics CTests run alongside all existing
transport, guest/ISR, storage, provisioning, frontend and legacy checks. Existing
TWI device-service tests now also verify observed bytes, ACK/NACK and STOP
without consuming receive data. The public guest fixture and submodule pins
are unchanged; `legacy` remains the default.

Local macOS Release/SVG checks also passed all 30 CTests, the OpenGL shell
regression, and 15 tooling unittest tests. Full profiling pytest is Linux-specific:
the local Mac lacks `os.sched_getaffinity` (125 passed, 56 failed, 2 errors).
That invocation is not a local pass; the unchanged Linux CI suite passed 183/183.

## Public output identities

Downloaded CI artifacts:

| Native configuration | Artifact ID |
|---|---|
| Linux, profiling OFF | `12881865819` |
| Linux, profiling ON | `12881542134` |
| macOS, profiling OFF | `12881786259` |

All 43 non-CLI-log files match byte for byte across the three configurations.
The two CLI text logs differ in host timestamps/paths. Native PCM, timestamped
word CSVs, guest event/state JSON and identity records match. In particular,
the existing 16-word public guest still emits 64 bytes with SHA256
`9e412b21be559d5c47c41f35835d565a31a05dc458747b28d251c39ad2fa7f44`.
Clockless PCM remains empty, SHA256
`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`.

## Private observation scope

The unchanged private MAIN and matching inputs were executed separately from
CI, with a preserved original baseline, snapshot storage, guest/wall bounds,
and no forced clock. Two final baseline runs matched all nine relevant output
files; two explicit synthetic-panel runs also matched. A replay from the
persisted private executable/inputs reproduced all nine conditional outputs.
The private record contains image/input/executable hashes, exact launch commands,
the recovered fixture source/provenance and raw traces; none is a public artifact.

Baseline panel identification fails before audio. The explicit compatibility
reply reaches the guest audio entry and its publication of a prefix-admission
refusal, before codec I2C or SPORT/DMA start. Both modes serialize zero words.
This differs from the later initial-zero qualification failure reported on
physical hardware. No codec output, board clock, unmute or hardware-equivalent
failure is claimed.

The next dependency is the reserved grouped-hysteresis write/readback or a
differing initialization-order explanation described in
[the startup contract](startup-diagnostics.md). No unsupported readback override,
codec identity or board oscillator was introduced to make the guest pass.
Codec/reset/clock routing remains an explicitly unresolved part of the milestone.

## Port G history checkpoint

Implementation: [`4db02251e0eaa164feb097cc08307db79d68041f`](https://github.com/GosuBoy/op1emu/commit/4db02251e0eaa164feb097cc08307db79d68041f).
[CI run 40848779291](https://github.com/GosuBoy/op1emu/actions/runs/40848779291)
passed all six jobs on 2026-10-29: Linux 31/31 CTests, Linux profiling 34/34,
macOS 31/31, Python 15+183, libc++ and the pinned GNU guest rebuild. Both Linux
GL shell checks passed; hosted macOS GL context was unavailable. Local native
CTest passed 31/31, including the actual guest access-pattern regression.

The dedicated register history and pre-run checkpoints are documented in
[the hysteresis investigation](portg-hysteresis.md). They preserve raw storage
behavior and report loader/guest/host origins, before/after values, boundaries,
raw-mapping gaps and explicit truncation. Synthetic tests cover the normal
16-bit read/bitset/low-half-store, byte/word/bulk lane mechanics, loader writes,
checkpoints and forced history overflow. Extra API widths do not qualify physical
MMR access widths.

Public native artifacts `12886646047` (Linux profiling), `12886396140` (Linux)
and `12886157509` (macOS) retain the existing capture identities. The public
clocked guest PCM remains 64 bytes with the SHA256 above; clockless output is
empty. All 43 non-CLI-log files match across these configurations and the prior
checkpoint's artifacts. Private firmware, inputs, provenance and raw histories
are absent from CI.

Two final fixed-reference runs match all 19 relevant files, including the
dedicated history and checkpoint RAM. A replay from the persisted private
executable/inputs reproduces those identities. The shared event stream, native
files and selected CPU/MMR/transport state match the preceding conditional
reference. History has one guest write and no loader/other-host write, omission,
raw mapping exposure or unexplained change.

No dispatch/width/lane or reset correction is justified. Initialization writes
eight; the admission snapshot and pre-comparison member contain eight, while
the guard requires whole-register zero. Admission still refuses before codec
I2C and SPORT/DMA. Hardware reserved-encoding readback and physical execution/
initialization order remain unverified; the historical note's candidate hash
association does not supply the missing transition capture. No hypothetical
readback override or artificially passing guard is included.
