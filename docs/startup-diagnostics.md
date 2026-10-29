# Bounded startup diagnostics

The normal CLI can record initialization without supplying clocks, replacing
guest memory, or polling DMA to make it advance. `legacy` remains the default
SPORT mode. Select `transport` explicitly when investigating serialization.

## Firmware-free reproduction

Build with `OP1_BUILD_HEADLESS_TESTS=ON`, then run:

```sh
ctest --test-dir build -R 'op1-(oled-bus-test|device-service-test|startup-diagnostics|sport-guest)$' --output-on-failure
```

These tests use synthetic GPIO/TWI traffic and the existing public Blackfin
guest. They run in all three native CI configurations. The guest, transport,
frontend, storage, provisioning and legacy tests remain registered.

For an individual CLI run, choose an output directory and supply the usual
`gui/ui.json` and input-script context. The existing public guest needs its
explicit synthetic clock to finish successfully:

```sh
./build/op1emu /tmp/unused.nand tests/fixtures/sport_guest/diagnostic.ldr \
  --headless --deterministic --nand-snapshot --input-script /tmp/input.txt \
  --sport-tx-mode transport --sport-tx-clock 1000000 \
  --max-cycles 2000000 --max-packets 2000000 \
  --diagnostics /tmp/guest-startup --diagnostics-memory 0x2000:128
```

An input script can contain `accel 0 0 800`, `volume 0`, `wait 30000`, `quit`
on separate lines. The existing guest test creates an isolated working directory,
UI configuration, script and synthetic OTP automatically. See its fixture README
for image/compiler identities. This public demonstration qualifies transport;
it does not qualify a codec or board clock.

For private startup, substitute the verified MAIN LDR and matching provisioned
NAND from the private recipe. Use a private output directory, snapshot storage,
an outer wall timeout and both execution bounds. Omit `--sport-tx-clock`.
Keep hashes, OTP/NAND contents, source, traces and RAM dumps outside this public
repository and CI artifacts. Record the exact executable, source revision,
arguments and input hashes alongside each run.

## Trace contract

`--diagnostics PREFIX` writes `PREFIX.events.jsonl` and `PREFIX.state.json`.
`--diagnostics-memory ADDRESS:BYTES` additionally saves a plain mapped RAM
range, at most 1 MiB per range. It rejects MMIO, boot ROM, unmapped, wrapping,
empty and oversized ranges. RAM ranges are read after CPU execution stops.

The event file contains first-seen run-entry PCs, periodic guest progress,
selected MMIO writes and first/changed reads, actual TWI callback bytes and
ACK/NACK/STOP outcomes, and panel command/read observations. PC attribution is
the current basic-block entry, not an exact instruction address. Direct RAM
accesses are not traced. TWI times describe device-service observations, not
electrical bus edges. ACK means the existing modeled peripheral accepted its
callback; a dummy peripheral ACK does not identify physical hardware.

Separate budgets reserve room for later events even when display traffic is
heavy: 100,000 first-seen PCs, 30,000 TWI events, 1,000 panel events, 10,000
progress events, and 50,000 MMIO events divided among GPIO, timers, TWI,
SPORT/DMA, interrupt and other controls. Their exact limits are in the first
event. The final report records capped categories, coalesced/omitted events,
per-address access totals, and the 128 most frequent run-entry PCs. A capped
trace is not proof that later transactions did not happen.

The final report includes stop reason, PC, CPU failure and saved return state,
registers, guest cycles/retired packets/IDLE cycles, SIC/CEC, GPIO/pin controls
(including hysteresis), SPORT/DMA/TWI controls, buffered-word state, acceptance
and serialization totals, and explicit external clock/FS availability. It avoids
FIFO data registers whose reads would consume input. Snapshots can settle
devices to the already stopped guest time; they do not advance guest time.
Raw register storage is not evidence of implemented electrical side effects.
Native SPORT PCM/word timestamps still use `--sport-tx-capture PREFIX`.

`--max-cycles N` and `--max-packets N` stop between calls to the normal CPU
runner and return 124. They can overshoot by one runner call, including its
IDLE interval. They do not interrupt a stuck host call; retain an outer wall
timeout. CPU execution failures return 1. Returning from an LDR, stopping a
run, or reaching an execution bound does not establish diagnostic success.

## Explicit synthetic panel reply

By default, unknown OLED commands have no invented response and clear any
previously selected register. In particular, an unsupported identification
read leaves the existing GPIO bus latch unchanged. The register-alias fix
prevents an unknown command from accidentally reading/writing an older known
register.

`--synthetic-panel-ddb HEX10` selects a five-byte software reply to command
`0x00a1`. For example, `--synthetic-panel-ddb 123456789a` replies with
`12 34 56 78 9a`. The profile is explicit, logged, and included verbatim in
the final state. The emulator never selects it from a firmware filename,
environment variable or boot-ROM revision.

The fixture observes CS, RS, RD and WR on the existing GPIO bus. It advances
once on an RD falling edge while selected in data phase; held-low RD does not
advance. Command and CS boundaries reset the cursor. It does not fabricate a
sixth byte, mask unrelated commands into A1, or alter guest instructions.
Initial inactive control levels and these boundary rules are fixture assumptions.
It does not identify a physical controller, establish meanings for its fields,
model panel reset wiring, or qualify display scanout. Exploratory results are
conditional on the exact selected reply. Software-selection flags from a
bootstrap are separate from the raw five-byte bus response.

## Private startup observation and remaining dependency

The hash-identified, unchanged private MAIN was run with its matching content
NAND, synthetic OTP context, snapshot storage and no forced external clock.
The original baseline stops at panel identification before audio initialization.
An explicitly selected recovered compatibility reply reaches the audio entry
but is then refused by an earlier board-control admission check, before any
codec transaction or SPORT/DMA start. Repeated native PCM is empty. This does
not reproduce the later silent-transmission stall reported on hardware.

The next unresolved behavior is grouped Port G hysteresis readback. The guest
writes a reserved two-bit field encoding; the current generic PORT_MUX storage
retains it, whereas admission requires a different value. The
[ADI BF52x Hardware Reference rev. 1.2](https://www.analog.com/media/en/dsp-documentation/processor-manuals/ADSP-BF52x_hwr_rev1.2.pdf),
sections 9-24/9-25, Figure 9-6, defines each group's `00`/`01` settings and marks
`1x` reserved. It does not specify the required readback for that write. Do not
silently clear it, change guest guards, or claim the generic storage represents
physical behavior. Resolve the hardware readback or explain the differing
initialization order before extending the unchanged reference run.

The recovered control sequence is consistent with a CS42L52 B1 identity and
register map. The [Cirrus DS680F2](https://statics.cirrus.com/pubs/proDatasheet/CS42L52_F2.pdf),
sections 4.6 and 6.1/6.5-6.10, defines identity, master/slave clock divisors,
serial format, tristate, power and mute controls. Address/ACK alone is
insufficient identification. A codec control-port model, reset-expander wiring,
verified board MCLK and its enable conditions, and clock/FS pin connectivity
have not been qualified or connected here. No board oscillator frequency is
invented. Codec output state remains unmodeled, rather than being inferred
from an empty native capture. Master clock generation, SPORT serialization and
mute/output routing require distinct evidence.

The private diagnostic's initial-zero qualification also requires a consistent
paired DMA observation, DONE/no error, retained ownership/routing and entry into
the leading-zero descriptor window before unmute. A remaining count alone is
not wire progress. DMA prefetch buffering, descriptor timing and SYNC-dependent
completion remain outside the qualified transport subset. These later checks
were not evaluated in this startup run; zero-valued result fields are not passes.
