# Standalone recovered mixer integration

The recovered sound-effects mixer and DMX decoder execute as Blackfin guest
code through the existing finite SYNC1 streaming consumer. This is a separate
standalone experiment. The recovered source, private build inputs, linker
maps, execution proof and captures are retained outside this public repository.
Public CI requires none of them. The legacy default, unchanged R4 refusal,
raw Port G hysteresis behavior and conditional panel experiment are preserved.

## Clock contract and public reproduction

[Cirrus DS680F2](https://statics.cirrus.com/pubs/proDatasheet/CS42L52_F2.pdf),
Table 1 on printed page 34 and registers 05–07, supports manual single-speed
operation at 44.1 kHz with 11.2896 MHz MCLK. AUTO=0, SPEED=01,
32kGROUP=0, VIDEOCLK=0, RATIO=00, MCLKDIV2=0 gives `05=20`.
The selected `06=80`, `07=00` master/left-justified configuration uses
64 serial clocks per stereo frame: SCLK=2.8224 MHz, LRCK=44.1 kHz.
The existing 12.288 MHz / 48 kHz profile is preserved. Both require guest
TWI writes and the declared synthetic RESET/wiring; no direct SPORT clock
is supplied. Supplies, MCLK, B1 identity and wiring remain test inputs.
Mute/output programming and upstream serial capture are separate observations.

```sh
cmake -S . -B build
cmake --build build -j6
ctest --test-dir build --output-on-failure
python3 tests/fixtures/stream_guest/build.py --as bfin-elf-as --ld bfin-elf-ld --check
```

The fixture rebuild uses stock GNU Binutils 2.44. The new CTest
`op1-stream-guest-44100` runs normal, delayed, premature-reuse, clock-pause,
permanent-clock-loss and diagnostic-stop cases. Each runs twice without
chaining and once with chaining; PCM, timestamped CSV and event JSON must
match byte for byte. Normal/delayed/premature cases also use the ordinary
headless CLI with synthetic MCLK, snapshot NAND and an execution bound.
CI retains these public captures alongside the previous fixture artifacts.

At 44.1 kHz, a 64-frame block takes 1.451247166 ms. The retained 4 ms
watchdog exceeds `2 * (64/44100 + 0.0005)` seconds, covering the declared
500 µs clock pause. Six buffered SPORT words take 68.027211 µs to drain;
the new guard is 70 µs (28,000 guest cycles). At 48 kHz, the established
65 µs guard and all three LDR image hashes are unchanged. The checker
compares timestamps against `index * 1e9/88200` with a one-nanosecond bound,
including the explicit 500 µs pause, rather than accumulating rounded periods.

The old delayed-refill failure remains observable: 59 replayed components
at 48 kHz, 43 at 44.1 kHz, followed by 31,488 fresh components in the wrong
channel slots. The new safe-stop case preserves 1,280 fresh components and
42 complete replays, snapshots TUVF, disables SPORT before DMA, and returns
`UNDR`. It submits no further buffer. One partial replay was in flight at
stop; no captured word is removed. Alignment recovery remains unsupported.
No host slot relabeling or codec phase reset is used.

## Private source-only integration

The private recovery appendix records the exact revision and SHA256 of the
unmodified `sfx_mixer.c/.h` and `dmx_sample.c/.h`, their API, generated DMX
identities, compiler/runtime identities and complete build/launch commands.
The recorded historical compiler binary was unavailable locally. Stock
GCC 13.2.0 was built for `bfin-elf`; every linked C and GCC Blackfin runtime
component was rebuilt from source using that compiler and Binutils 2.44.
No historical object or previous emulator transport sink is linked. This
does not claim reproduction of the old V74 executable.

The DMX decoder accepts the generated format-3 header, unsigned 8-bit sample
data and its 16 leading/trailing pad samples. Three inputs exercise a constant
signal, a deterministic ramp and an alternating signal at 44,100, 32,000 and
11,025 Hz. The mixer's existing integer/remainder source-rate conversion is
explicitly tested; output is initialized to 44,100 Hz throughout. There is
no output-rate conversion or relabeling of the resulting capture.

Thirteen sample-indexed events (seven starts and six stops) cover a single
voice, overlap with saturation, hard left/right panning, centered and unequal
gains, stops and retriggers. Events split rendering at exact sample boundaries,
including positions inside both 64- and 512-frame chunks. Voice state remains
in the recovered mixer across calls. A separate no-event control uses the
same decoder/mixer with silent output.

The adapter renders 64 signed 16-bit stereo frames into guarded staging,
then packs each sample as `(uint32_t)(uint16_t)sample << 16` into its FILLING
slot. The public consumer performs SSYNC and publishes READY. Finite 32-bit
DMA4, SYNC1/DI_EN, count 128 and modify 4 are unchanged. The ISR acknowledges
completion; main context releases the buffer and renders future data. No
substantial mixing runs in an ISR. Private C startup initializes static
state, paints a linker-owned 64 KiB stack and sets eight storage guards.
Mailbox, buffers, ledger and static state have disjoint linker reservations;
there is no heap or allocator. ISRs save/reload P5 because C can temporarily
use the assembly consumer's mailbox register. The producer wrapper reserves
the Blackfin argument home area and preserves its assembly caller state.

The private runner invokes the public native observer through the normal
LDR loader. Host actions supply declared MCLK transitions and read state;
guest instructions perform decoding, events, mixing, packing, buffer
publication, DMA submission, IDLE and interrupt handling. Cycle, Run-call
and wall limits bound failures. PC, CEC, DMA, SPORT, ownership, bounded
ledgers, guards, stack observations and word counts are retained on failure.

```sh
# PRIVATE_MIXER_GUEST points to the separately retained private fixture.
python3 "$PRIVATE_MIXER_GUEST/build.py" --repo "$PWD" --gcc "$BFIN_GCC"
python3 "$PRIVATE_MIXER_GUEST/run.py" --repo "$PWD"
```

The retained private toolchain recipe pins official GNU source archives and
build dependencies, including the Darwin host-header workaround. The build
receipt includes every target compilation/link command, source digest, ELF
symbols, stack-usage files, image hash and generated input hash. Its local
runner is deliberately outside the public repository and CI artifacts.

## Evidence and limits

Each complete private run captures 141,312 stereo frames: exactly
`141312/44100 = 3.204353741…` seconds, 282,624 packed words. It executes
2,208 source productions and DMA interrupts, with 1,104 productions per slot.
The event schedule reaches beyond 2.8 seconds. Every sample is compared with
an independent integer oracle using closed-form source indices, panning,
signed truncation and saturation. A separate native execution of the exact
recovered sources also renders the entire schedule with both 64- and
512-frame calls; both must equal the mathematical oracle and guest capture.
The event ledger checks all 13 actual event indices and parameters.

Separate PC observations establish entries into the recovered parser,
initialization, render, start and stop functions. Every guest ownership
transition, ISR W1C and return address is checked. DMA memory-read and
peripheral-acceptance digests are compared separately from serial samples;
each SYNC1 completion still precedes the final six serialized words. Tests
require no steady-state replay, correct channel slots, no trace drops,
intact guards, bounded stack use, and byte-identical PCM/CSV/event traces
across repeated/chained runs. One final post-data underflow is accounted
while the normal drain guard expires, as in the existing streaming test.

The private matrix also repeats no-event silence, the bounded producer-delay
stop, a 500 µs MCLK pause and permanent MCLK loss. The clock-loss case freezes
all ten DMA/SPORT slots and terminates through the guest 4 ms watchdog.
It preserves the partial DMA state rather than inventing a completion.

See [validation results](doom-mixer-validation.md) for the measured identities
and CI result. Full-game integration still needs its provider/event adapter,
complete source/runtime/linker/loader context, any private assets, and a
qualified driver for the modes it selects. Hardware codec identity, MCLK,
RESET/board routing, physical hysteresis readback, unchanged V74/R4 startup
and analog output remain unqualified. No physical device access is involved.
