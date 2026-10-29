# Sustained guest-driven streaming

This milestone qualifies an independent public producer/consumer test on the
opt-in 48 kHz and 44.1 kHz CS42L52 profiles. It does not identify a physical OP-1 codec,
clock/reset route or deadline margin. R4's unchanged admission refusal and raw
hysteresis readback are preserved. `legacy` remains the default.

## Recovered source contract and selected mode

Accessible private materials were inspected without copying their code or
assets into this repository. Exact private source/toolchain pins and recovery
dependencies are retained in a separate private source appendix.

| Source/context | What the source establishes | What it does not establish |
|---|---|---|
| Earlier emulator gameplay checkpoint |44.1 kHz signed 16-bit interleaved stereo, 512-frame static mixer staging, packed uint16 << 16, finite DMA4 count 1024/modify4/config0089. Serialized sleep/clock callbacks poll DONE/RUN and reuse staging. Stop cancels outstanding data. | Hardware codec initialization, continuous scheduling, IRQ-driven streaming or physical drain. Its SPORT011f/1 uses the old legacy bit decode and clock bypass. Its DONE-implies-RUN0 assumption is not valid with SYNC0 FIFO prefetch. |
| Earlier standalone/queue checkpoint |64-frame stereo signed 16-bit blocks; three software slots plus silence; FREE/FILLING/READY/IN_DMA states and generation/sequence tokens. Finite0089 transfers, bounded polling. | This software descriptor/token is not a hardware DMA descriptor. The queue assumes serialized callers and does not prove interrupt/cache publication. |
| Hardware-intended R4 tone diagnostic | SPORT4401/021f, 32-bit words, a permanent five-node large-descriptor graph, seven-halfword descriptors and SYNC0. Selected DI_EN/DONE observations, source 17 masked, selective main-context W1C. | A refillable Doom mixer driver, exact physical MCLK/rate or successful unchanged audio startup. The preserved reference refuses before this initialization. |

The public test selects the smallest finite mode needed to exercise the
producer lifecycle: **two 64-frame buffers, 32-bit linear FLOW=STOP DMA4,
SYNC1/DI_EN (`00a9`) and repeated guest restarts**. It is explicitly synthetic,
not OP-Doom's verified hardware configuration. It retains the codec profile's
12.288 MHz synthetic MCLK, 3.072 MHz SCLK, 48 kHz stereo, left-justified 24-bit
payload/32-bit slots and declared PG12 RESET/serial wiring. The source sample
representation is signed 16-bit packed in the slot MSBs. Mute requests remain
asserted; native capture is upstream SPORT evidence, not a DAC output.

The [standalone private mixer integration](doom-mixer-integration.md) now
replaces this producer at an explicitly initialized 44100 output rate. Its
source, build inputs and captures remain private. The public 44.1 kHz fixtures
qualify the same consumer without requiring those sources. No 44.1 kHz stream
is interpreted as 48 kHz.
Descriptor traversal is unnecessary for this selected mode and remains a
separate qualification task for hardware-intended R4.

## DMA qualification and correction

Primary source: [ADI BF52x Hardware Reference rev1.0](https://www.analog.com/media/en/dsp-documentation/processor-manuals/BF52xHRM_Rev.1.0.pdf),
printed 6-6 (channel FIFO and memory/peripheral sides), figure 6-2 on 6-20,
6-22 (startup memory reads/current registers) and 6-23 (SYNC/refresh/RUN).
The accessible indexed primary extracts establish these functional rules;
full PDF downloads failed in this environment. The equivalent description in
[ADI BF51x rev1.2, 6-25/26](https://www.analog.com/media/en/dsp-documentation/processor-manuals/adsp-bf51x_hwr_rev1.2.pdf)
corroborates SYNC0/1 and disable flushing. Bus arbitration and exact latency
are not inferred from cross-family timing tables.

Inspection and the existing eight DMA/SPORT/codec tests preceded changes.
Previously the transport path read source memory only as SPORT accepted it,
and ignored SYNC. That conflated two boundaries and omitted controller-held
data. The opt-in transport now has a **separate four-word DMA FIFO** for
32-bit, 1D, NDSIZE0, FLOW=STOP memory reads. It fills independently of SPORT room
or clocks; reads copy immutable values into the controller FIFO.

| Event | Meaning for the qualified mode |
|---|---|
| Memory read | Source address/count advance. The copied word is now independent of later source-RAM stores. Up to four words can await SPORT. |
| SPORT acceptance | A copied DMA word enters SPORT's FIFO. SPORT can additionally hold/shift words; this is not serial completion. |
| SYNC0 completion | Final source word read; DI_EN publishes DONE/interrupt while controller data may remain. RUN remains 1 until that FIFO drains. This is a safe source-reuse boundary, not peripheral shutdown. |
| SYNC1 completion | Final word accepted by SPORT, controller FIFO empty, RUN0 and DONE/interrupt. Source reuse is safe; up to six SPORT words may remain. |
| Serial completion | The word's32 serial clocks have completed. Only here is it captured. |

Completion acknowledgment is W1C; zero does not clear it, acknowledgment does
not discard buffered data, and later FIFO drain does not repeat the SYNC0 IRQ.
DMA disable discards the controller FIFO while preserving already accepted
SPORT data. The streaming guest only disables after controller drain; it stops
SPORT first to avoid exposing a PIO request on the shared interrupt.

`op1-dma-sport-fifo-test` independently checks read/accept boundaries, both SYNC
settings, RUN, DI_EN0, W1C, disabled peripheral prefetch, safe source mutation
after final read, premature mutation before read and controller disable. The
streaming guest checks the actual CPU/SIC/CEC path. Model observations distinguish
read, accept, complete and drained events, with per-work counts/digests.

This is **functional ordering at CPU service times**, with no invented DMA bus
delay. Hardware bus arbitration, cache coherence, descriptor fetch/prefetch,
2D/autobuffer/list modes and memory-space transitions are not qualified.
Other widths/modes and the legacy backend retain their earlier approximation;
`TxSnapshot.modeled` explicitly identifies the new finite 32-bit path. The current
memory counters are not cycle-accurate BF524 bus measurements.

### Changes to earlier finite tests

Earlier SYNC0 tests now observe final memory reads rather than acceptance.
In the 16-word, 32-bit tests, cold no-clock state is four SPORT words plus four
DMA words: source count 8/address3020, not12/3010. DMA interrupt entry occurs
after six serialized words rather than ten; DONE|RUN is 9 before W1C and RUN8
afterward. Four controller plus six SPORT words may remain.

Accordingly, the 1 MHz guest's guard becomes 321 us (10*32 us=320 us plus margin),
and the codec3.072 MHz guest's guard becomes 105 us (10*32/3.072 MHz=104.1667 us
plus margin). Their image/event identities change intentionally. PCM and
serial CSV remain byte-identical to the preceding milestone; source counts,
IRQ observations and drain-cycle evidence change. Earlier validation documents
retain their commit-bound historical identities and link here for the correction.

## Guest producer and ownership

The [public fixture](../tests/fixtures/stream_guest/README.md) loads normally at
ffa00100, executes reset/TWI setup and fills only two initial buffers. It then
produces all subsequent data while the other slot transfers. No host code
writes MMIO, refills samples, advances a peripheral or injects completion IRQs.

Each 512-byte slot holds64 stereo signed 16-bit frames packed into 128 32-bit words.
For block b0..255, channel c0..1, frame f0..63, the sample bit pattern is
`((b+1)<<7)|(c<<6)|f`, packed as uint16 << 16. It uniquely identifies generation,
channel and frame, including negative signed 16-bit values near the end. The oracle
calculates values independently and checks every native word and timestamp.

The guest checks FREE before filling, publishes READY after SSYNC, verifies
generation/READY before submission, marks IN_DMA and releases only after its
IVG9 ISR has acknowledged DONE and returned with RTI. It starts the next ready
work unit before refilling the released slot. Every one of 256 completions is
recorded. Each slot is produced 128 times:127 reuses after its initial fill.
At release, the last six words still buffered by SPORT must survive the refill;
the independent native oracle verifies this across every boundary.

Restarts check RUN/error/IRQ status0 and rewrite CONFIG with DMAEN still1.
Disabling DMA during a starved restart would expose SPORT's PIO service request
on the same SIC17 line; W1C DMA_DONE cannot acknowledge that request. The delayed
producer test exposed this guest-side trap. Keeping stopped DMA ownership
avoids it without changing the interrupt model. Source17 routes to IVG9;
other peripheral sources are masked. SPORT error status/counters and repeated
flags are observed independently; this fixture does not install a SPORT ISR.

The final SYNC1 completion leaves at most six SPORT words. Their serialization
takes 62.5 us at this clock. A guest core-timer65 us guard, with bounded handler/
main overhead under the remaining slot margin, stops before any completed
replay word. The empty next slot latches one explicitly accounted-for shutdown
underflow. Exact positive output has 32768 fresh components, no completed
repeats, no overflow and no steady-state underflow. This guard is derived for
the selected format/rate; it is not a generic drain register or hardware-safe
analog shutdown sequence.

## Positive and negative results

| Case | Result checked independently |
|---|---|
| Normal |256 IRQs, 32768 correct fresh words/16384 stereo frames, zero swaps/drops/stale/repeated/invalid words.64-frame blocks represent 341.333333 ms at 48 kHz; total guest duration345.699550 ms. |
| Delayed refill | Guest waits 2 ms before producing block 10 while block 9 transfers.59 repeated last-word components complete; payload generations otherwise remain correct. Odd replay count shifts LRCLK phase:31488 later fresh components occupy the opposite channel slot.60 underflow occurrences include the final shutdown one. The test detects and reports this failure of continuous stereo playback. |
| Premature reuse | Guest tri-states serial pins, verifies8 source words read (4 SPORT+4 DMA), overwrites the IN_DMA buffer, then restores pins through TWI. First8 words retain their copied values; exactly 120 unread components acquire the wrong generation. No replay/underrun hides the lifetime error. |
| MCLK pause/resume | Same normal image; declared external MCLK drops 500 us into work 33 and resumes 500 us later through normal CPU events. Data/phase remain correct; timestamps contain exactly 500 us added pause, with no catch-up burst. |
| MCLK remains stopped | Same normal image; guest 4 ms watchdog returns WDOG after 32 completed blocks and 4138 serialized components. The partial33rd work remains visibly stalled; failure diagnostics retain its buffers/state. |

Clock-stall observations are passive: DMA FIFO4, SPORT FIFO4, holding+shifting1
each, ten total outstanding words. At both separated observations source work 33
has read 52/accepted 48/remaining 76, with unchanged state and no invented underrun.
Successful pause/resume does not fire the watchdog. Clock removal during active
power is an intentional digital negative stimulus, not a valid analog power-down.

CSV retains timestamps, physical slot, index, valid and repeated flags. PCM
includes valid replay words, so concatenation cannot hide them. The oracle checks
the exact rational slot cadence (32/3.072 MHz=10416.6667 ns), the explicit pause,
LRCLK order, per-block source/accept digests, ownership events and saved ISR
return addresses. The delayed block 9 interrupt returns to the guest's delay
IDLE; normal completions return to its regular IDLE.

## Reproduce and diagnose

```sh
cmake --build build
ctest --test-dir build -R 'op1-(dma-sport-fifo|stream-guest)' --output-on-failure
python3 tests/stream_guest_integration.py \
  build/op1-stream-guest-test build/stream-guest build/op1emu
python3 tests/fixtures/stream_guest/build.py --as bfin-elf-as --ld bfin-elf-ld --check
```

The committed source/linker/build script and three image hashes are in the
fixture manifest. GNU Binutils2.44 is sufficient; no C compiler/runtime or
proprietary input is needed. CI independently rebuilds all public guests.

For the normal positional frontend in a scratch workspace with gui/ui.json,
use an erased/missing snapshot NAND and a bounded input script:

```sh
printf 'wait 30000\nquit\n' > /tmp/stream.input
op1emu missing.nand /absolute/path/to/tests/fixtures/stream_guest/normal.ldr \
  --headless --deterministic --nand-snapshot --twi-speedup 1 \
  --sport-tx-mode transport --cs42l52-test-mclk 12288000 \
  --sport-tx-capture /absolute/path/to/stream \
  --max-cycles 200000000 --input-script /tmp/stream.input
```

No direct `--sport-tx-clock` is used. Normal, delayed and premature images are
also executed through this frontend and their PCM/CSV must match the CPU driver.
All five cases run twice without chaining and once with chaining; PCM, CSV,
register/event JSON and metrics must match. CI preserves cross-platform native
capture comparison evidence.

Limits are 200 million guest cycles, 3 million Run calls and 30 seconds wall time per
driver run. A short-budget test must fail with diagnostics. JSON includes PC,
CEC/exception/mailbox, DMA memory/FIFO state, SPORT counters/buffers, codec/TWI,
clock inputs, error observations and all per-block ownership/completion evidence.
The source read/accept digests cover every word; only the explicitly selected
first 16 reads are expanded. Independent bounds are 512 work records, 2048 guest
records, 2048 codec and 2048 TWI events, 16 clock snapshots and 1024 SPORT error
observations. Dropped counters are explicit and nonzero fails. Guest event
CYCLES values are basic-block snapshots; peripheral events use guest time.

## Connecting the real mixer

`produce`/`fill_words` isolate synthetic generation from submission, codec,
SPORT/DMA and interrupts. Replace production with a serialized64-frame signed 16-bit
stereo mixer adapter, pack into an owned released slot, then publish READY.
Do not transplant the earlier emulator-only sink or its clock/SPORT bypass.
The fixture's standalone addresses must become linker-owned native reservations.

The recovered private mixer/queue source and source-only emulator checkpoint
are available. The exact legacy Linux x86-64 Blackfin GCC, reused-object/source
manifests, private V74 build/boot inputs, WAD and relocated build context are
separate integration dependencies identified in the private recovery appendix.
No hardware-qualified live Doom mixer/DMA driver was established by those
materials. Descriptor/prefetch/cache qualification and physical codec/board
evidence remain separate work. Passing the emulator's400 MHz packet/IDLE timing
model establishes neither BF524 deadline margin nor smooth real-time host playback.

See [validation and output identities](streaming-validation.md). R4's baseline,
explicit synthetic panel status, disputed guard and reserved hysteresis storage
are unchanged. No physical device access or flashing is involved.

## Additional 44.1 kHz and safe-stop qualification

`normal-44100`, `delay-44100` and `premature-44100` use synthetic 11.2896 MHz
MCLK, with guest TWI selecting 05=20/06=80/07=00. SCLK=2.8224 MHz; each
component takes exactly 1e9/88200 ns. The independent checker compares rational
timestamps over all 32768 components, rather than summing rounded periods.
A block lasts 64/44100=1.451247166 ms. The retained 4 ms watchdog exceeds
twice this duration plus the declared 500 us clock pause. Six SPORT words
need 68.027211 us after SYNC1 DMA completion; the new drain guard is 70 us
(28000 guest cycles), below a seventh complete slot. The original 48 kHz
images and 65 us guard are byte-identical to the previous checkpoint.

The existing delayed-refill regression remains intentionally failing as audio:
at 44.1 kHz it captures 43 repeated components and 31488 fresh components with
wrong stereo phase; at 48 kHz the established counts remain 59/31488.
`stop-delay-44100` instead selects `STREAM_SAFE_STOP`: guest code checks
SPORT TUVF before submission, before/after production, and before continuing
a wait. It snapshots the fault, disables SPORT before DMA and returns `UNDR`.
The injected 2 ms producer delay yields 1280 correctly ordered fresh components
plus 42 explicitly flagged replays and a partial repeat in flight. No later
fresh data is submitted; no host slot relabeling, clock reset or sample removal
occurs. Stereo recovery remains unsupported. Final normal drain's one
post-data underflow is accounted separately and does not trigger recovery.

The public consumer also accepts assembly build parameters for block count
and ledger capacity. The native test observer accepts linker-provided mailbox/
ledger addresses, bounded memory observations, stack paint inspection and PC
watch counts. These are passive observations; none refill data or advance
peripherals. The optional PC counts are sampled at CPU Run boundaries and
are used for a separate unchained execution proof. Full repeat/chaining
comparisons run without that observation, since chaining can skip such
boundaries. All trace capacities remain explicit; overflow/truncation fails.
