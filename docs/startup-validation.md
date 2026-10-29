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
