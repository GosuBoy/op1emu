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
Commit-bound CI and artifact receipts follow after branch validation.
