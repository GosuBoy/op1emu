# Deterministic stop, inspect and resume

`op1-debug` provides a local, read-only debugger for a single direct RAM DXE.
It uses append-only JSONL command files and a separate JSONL response log.
It has no network listener, GUI, GDB service, guest patching or host audio
playback. The CPU implementation is independent of Linux profiling.
Existing frontend execution and the legacy transport default are unchanged.

## Exact stop contract

`CpuDebugger` attaches on the CPU owner thread between `Run` calls, requires
deterministic time and rejects an attached host audio output. Attachment
disables JIT chaining and invalidates existing translations and pending
promotions: changing a compiler setting alone would leave previously compiled
native loops capable of skipping stops. Chaining cannot be enabled while the
debugger is attached. Existing block compilation/optimizations remain in use.

Two supported boundaries exist:

- **Run entry:** before the next block/packet executes, after the preceding
  Run's normal device/event/interrupt service and any completed IDLE wait.
  The initial entry has only construction/loader state; no extra initial
  service is invented.
- **Before an IDLE wait:** the IDLE packet has retired and the normal service
  pass has completed, but guest time has not jumped to a future wakeup. The
  reported PC is the exact continuation after IDLE. It may subsequently be
  redirected by an interrupt during the preserved wait, before that packet
  executes. `idle_wait_pending` identifies this phase.

The service pass retains its existing order: check pending CEC state, service
devices, advance timers, service panel and queued events, latch interrupt
lines, then check pending CEC state again. Stopping adds no service pass.
Repeated inspection never executes that work. Continue performs a deferred
IDLE wait exactly once, at its original guest deadline, before another packet.
The raw core `idle` field records the last core exit; it is not a physical
power-state model.

PC requests accept even addresses in passive plain RAM, with at most 64
armed targets. They do not split blocks, decode arbitrary packet boundaries
or replace instructions. A target becomes verified only when that **exact**
PC is observed at a supported boundary; hits count actual stops. An address
inside an executed block can remain unreached, explicitly reported in target
status. No nearby PC is substituted. Continue suppresses the current stop
until guest packets execute; a later genuine revisit can stop again, including
a self-loop. IDLE time passing alone does not create a revisit.

`HaltExecution` delegates to a debugger pause request; it requires an attached
deterministic debugger. Only the request operation is safe from another
thread. Inspection, target changes, status and continue enforce owner-thread
access. The command interface polls and executes on that same owner, rather
than reading a running CPU from a UI thread. Use predeclared guest-time inputs;
live host devices and real-time playback are outside this mode.

## Passive state inspection

Snapshots include R0–R7, P0–P5, SP/FP/USP/KSP, returns, ASTAT, accumulator
extension/word pairs, DAG and hardware-loop registers, SEQSTAT/SYSCFG/EMUDAT,
raw latched CYCLES/shadow values, and current guest time separately.

The selected device view includes DMA4 address/count, FIFO values and source
addresses, completion/RUN/SYNC and read/accept counts; SPORT0/1 FIFO, holding
and shift words, remaining bits, slots, configuration, counters, errors,
deadlines and external inputs; codec configuration/reset/MCLK/mutes; CEC/SIC
pending/masked/routing state; core timer count/expiry, GP service/clock
schedule, panel/TWI deadlines and queued event deadlines. At most 64 event
deadlines are listed, with total count and explicit truncation. Callback
functions are neither copied nor invoked by inspection.

These are passive snapshots, without guest-visible MMIO reads, FIFO pops,
flag clearing, peripheral advance or interrupt service. RAM reads return
1–4096 copied bytes only after validating the complete range. Unmapped,
wrapping, MMIO, hooked, subclass/device/ROM and watched/deferred-DMA pages
are rejected. Deferred memory DMA is not settled by a debugger read; inspecting
such memory is unsupported in this increment.

## Local session

Build with the repository's normal CMake setup:

```sh
cmake --build build --target op1-debug
python3 - <<'PY' > /tmp/op1-debug.commands.jsonl
import json
m = json.load(open('tests/fixtures/stream_guest/manifest.json'))
pc = m['images']['work-44100']['symbols']['work_wait']
for command in [dict(cmd='break', pc=pc), dict(cmd='continue'), dict(cmd='wait-stop')]:
    print(json.dumps(command))
PY
./build/op1-debug --ldr tests/fixtures/stream_guest/work-44100.ldr \
  --commands /tmp/op1-debug.commands.jsonl --log /tmp/op1-debug.jsonl \
  --codec-mclk 11289600 --capture /tmp/op1-debug-audio \
  --host-timeout-ms 300000
```

The process starts paused. In another terminal, follow its log and, after
the `stop` record, append commands:

```sh
tail -f /tmp/op1-debug.jsonl
# In a separate shell, or after stopping tail:
printf '%s\n' '{"cmd":"registers"}' '{"cmd":"audio"}' \
  '{"cmd":"ram","address":"0x2000","bytes":256}' \
  >> /tmp/op1-debug.commands.jsonl
printf '%s\n' '{"cmd":"continue"}' '{"cmd":"wait-stop"}' \
  >> /tmp/op1-debug.commands.jsonl
```

Commands are `pause`, `continue`, `break`/`remove` with `pc`, `status`,
`registers`, `snapshot`/`audio`, `ram` with `address`/`bytes`, `wait-stop` and
`quit`. Numbers can be unsigned JSON integers or hexadecimal strings. An
optional `id` is echoed. `wait-stop` subscribes to the next stop notification;
it never blocks later pause/quit requests. Wait for that notification before
sending inspections; inspection while running is rejected. Remove a target
using its exact PC to continue through all future visits. Symbol manifests
can be read by the client, as above; symbols never overwrite guest memory.

Only complete newline-terminated commands are consumed. Lines are bounded to
4096 bytes; each poll processes at most 128 commands. Files must remain regular
and append-only. Outputs must be distinct from image/commands/capture paths.
Use a private destination for private addresses, symbols or RAM.

`quit` works while running or stopped and produces an explicit finish record.
The independent host limit includes developer pause time and returns 124 with
`host-timeout`; it is not a guest watchdog result. Defaults are 30 seconds
wall, 6 billion guest cycles and 200 million Run calls. Wall time can be set
up to one hour; guest cycles/Run limits also terminate explicitly. A natural
LDR return reports remaining unreached targets. Registers/memory writes,
stepping, watchpoints, reverse execution and persistent snapshots are absent.

## Validation scope

The public CPU and integration regressions check compiled-code attachment,
branch/call/ISR stops, genuine revisits, unreachable targets, off-thread pause,
owner enforcement, invalid requests, clean quit and independent host timeout.
Actual streaming guests compare original, debug-enabled without stops and
resumed execution byte for byte: complete PCM, serial timestamps and event
JSON. Repeated snapshots across 50 ms host pauses remain identical during
instruction work, interrupt entry, submission and IDLE with four queued clock
events. The existing clock-pause/loss and legacy regressions remain registered.

The unchanged private 350-tic gameplay image is compared separately against
its preserved reference. Debug-enabled and stopped/resumed runs retain all
350 game tics/frames, gameplay/sound/ownership/interrupt ledgers, selected
pixels, guards and exact native PCM/timestamps. Private stop contexts cover
DMA interrupt entry, renderer execution and submission, with 50 ms host pauses
and read-only mailbox/buffer/stack inspection. Only separate command logs,
console logs and host timing metadata are excluded; guest artifacts are not
normalized. Private inputs, symbols and traces are absent from public CI.

This remains the existing synthetic RAM guest environment. Physical debugger
behavior, unchanged V74/R4 startup/admission, board routing and analog audio
are unqualified. See the [validation receipt](deterministic-debugger-validation.md).
