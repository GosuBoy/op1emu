# Deterministic debugger validation

Local Apple silicon validation passes all 38 public CTests, including the new
CPU boundary and debugger integration tests. The public integration compares
original, enabled-without-stops and paused/resumed execution using real
streaming guests. Complete PCM, CSV timestamps and JSON are byte-identical.
Fifty-millisecond host pauses preserve repeated snapshots, guest time, pending
DMA interrupts, watchdog deadlines and queued clock events. Pause and quit
remain responsive behind an unreached target; malformed requests and an
independent host timeout have explicit outcomes.

The unchanged private gameplay image and inputs are retained outside public
Git/CI. A fresh uninterrupted run matches the preserved reference, and both
debug-enabled and stopped/resumed runs match that same reference byte for byte
across PCM/CSV/JSON and five selected framebuffer files. All 350 tics/frames,
48 sound commands, 882,048 native words, 6,891 DMA interrupts, 974 renderer
interrupts and guard results are unchanged. Private read-only snapshots cover
DMA ISR entry, renderer execution and DMA submission, each with a minimum
50 ms host pause. Command/console logs and host timing metadata are separate;
no guest artifact fields or capture timestamps are rewritten or excluded.

See the [supported contract and reproduction commands](deterministic-debugger.md).

## Commit-bound CI and artifact evidence

Implementation [c662f1901abb96e4c7a12b6b7766f3ee92f90bc1](https://github.com/GosuBoy/op1emu/commit/c662f1901abb96e4c7a12b6b7766f3ee92f90bc1)
passed [CI 40914921454](https://github.com/GosuBoy/op1emu/actions/runs/40914921454)
in all six jobs: Linux OFF 38/38, Linux ON 41/41, macOS 38/38, ten unchanged
Blackfin fixture images, Python 15+183 tests and the libc++ time contract.

The subsequent [test hardening](https://github.com/GosuBoy/op1emu/commit/373096e950c475c1a7872fd42e8d6091abb2c8ac)
adds explicit zero hook/settlement-callback assertions and complete-line
response parsing. Its emulator/debugger implementation is unchanged.
[CI 40916758992](https://github.com/GosuBoy/op1emu/actions/runs/40916758992)
has successful Linux OFF 38/38, Linux ON 41/41, fixture, Python and libc++
results. The initial macOS job and its isolated retry were both cancelled before any
steps ran. During the bounded readiness closeout, one further targeted retry
(attempt 3) executed setup/build/test/upload and passed 38/38 at this exact
revision, closing the hosted-Mac gap. The cancelled attempts remain historical
facts; their cause is not established. See the
[readiness receipt](firmware-readiness.md) for the job/artifact identities and
separate coverage of the subsequent workflow changes.

All 245 logical PCM/CSV/JSON files and both scripted debugger JSONL logs match
local output byte for byte in the final Linux artifacts and the successful
implementation macOS artifact. All 224 preceding public data/identity files
are unchanged from 40904405319. Console logs and command-file metadata are
excluded; no guest data or timestamps are normalized. Every artifact member
is a public sport/codec/stream/debugger fixture. Private scene inputs, symbols,
RAM, frames and captures are absent.

| Source | Artifact ID | Archive SHA256 |
|---|---:|---|
| Linux OFF, test hardening |12916406981|`da7f1c33560744fabc5952b1d06d1fcf2b7a62ce429e40e244db256082706b1a`|
| Linux ON, test hardening |12915099824|`fc150cc480a190ff39cff266c2c6445516727f7c9461f81ed22c559a6d7d98b2`|
| macOS OFF, implementation |12915498049|`791f11870cd75060ef7948578a9f23d62404772bd6e0ac5b26b334e114f5e725`|

The hardened-test macOS artifact `12920716563` has SHA256
`b2c37edaa773371c3ff56579369f394331f5cff0a21860f98dc54f4a74e59de1`.
All 247 logical files match final local output and the two Linux artifacts.
