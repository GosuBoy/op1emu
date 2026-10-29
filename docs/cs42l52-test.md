# Independent CS42L52 digital component experiment

This opt-in component model is independent of private R4, its admission guard,
and the disputed Port G register. It qualifies a declared digital test setup,
not an installed OP-1 codec, revision, reset wire, oscillator or analog output.
The existing `legacy` default and default I2C `0x4a` dummy are unchanged.

## Primary specification and bounded contract

Source: [Cirrus Logic CS42L52 DS680F2, 2013-03-01](https://statics.cirrus.com/pubs/proDatasheet/CS42L52_F2.pdf).
The implementation was authored from this public specification. The older
private checkpoint's logical codec model is not an implementation dependency.

| Datasheet reference | Implemented experiment |
|---|---|
| Reset timing, p.20; 4.8–4.10 | RESET initially low; guest holds it low for at least 1 ms. Registers reset on assertion. Control access is available after a valid release, independently of audio MCLK. Shorter pulses fail closed as out of contract; this does not claim physical short-pulse NACK behavior. |
| 4.12, 6.1 | AD0=0, address `0x4a`; declared B1 test identity `0xe3`, read-only. MAP low seven bits, auto-increment bit7, byte fragmentation and STOP retaining MAP. Read uses MAP write/STOP followed by read, as specified. |
| 5, 6.2–6.7 | Defaults for supported controls: `02=01`, `03=07`, `04=05`, `05=a0`, `06=00`, `07=00`. Global power, output power fields, master/slave and serial tri-state are observable separately. |
| 4.11 | Accept and record the published hidden-register initialization sequence `00:99, 3e:ba, 47:80, 32:bb, 32:3b, 00:00`. Guest reads `32` and preserves its other bits. Completion records programming, not analog current/threshold performance. |
| 4.6 table1; 6.5–6.6 | Only 12.288 MHz MCLK, `05=20`, `06=80`, `07=00`: manual single-speed, normal ratio/polarity, master, left-justified. Derived LRCK=48 kHz, SCLK=3.072 MHz, 64 clocks/frame. |
| 6.7, 6.12–6.14 | Slave has no generated serial source; `3ST_SP` removes master pin drive. `0d` master-mute bits and `0f` output-mute bits are recorded independently of serial clocks. |
| 4.10 | A power-down request records a 100 µs settling deadline. Removing MCLK before that interval, or during active power, records `early_mclk_removal`; no analog effect is simulated. Unit checks straddle the deadline. |

Supported register addresses are `01..07`, `0c..0f`, and the narrowly accepted
`00/32/3e/47` initialization sequence. Other register accesses fail explicitly. Reads of hidden command addresses
`00/3e/47` are unsupported rather than inventing undocumented readback.
Writes to known controls retain readback; unsupported clock/format combinations
produce zero qualified clock output and an explicit reason, never a fallback
rate. Reserved bits of `02`, `03`, `07` must retain defaults. The complete
datasheet register map, autodetection, other rates/formats, ADC, DSP, mixing,
gain, analog supplies, charge pump, output routing and conversion are not modeled.

The qualified clock contract is the powered-up configuration above. Serial-pin
waveforms during global power-down remain outside this model: it supplies no
SPORT clock and reports `powerdown_clock_waveform_unqualified`. This is a
deliberate scope boundary, **not evidence that PDN physically stops SCLK**.
The datasheet's reference/charge-pump startup, initial muting and one-LRCK
frequency-ratio acquisition are not replaced by an invented analog-ready delay.
There is no modeled analog-ready signal or DAC output. The 100 µs field is
shutdown-sequence observation, not an analog simulation.

The existing TWI controller models byte-paced callbacks, not electrical I2C
waveforms. A reset-time peripheral rejection becomes its existing transfer-error
path; this does not qualify the physical address/data NACK distinction.

## Declared synthetic wiring and guest execution

`--cs42l52-test-mclk 12288000` with `--sport-tx-mode transport` selects:

- stable synthetic supplies and 12.288 MHz MCLK, initial asserted RESET;
- synthetic PG12 output to codec RESET, AD0 grounded;
- codec SCLK/LRCK to SPORT0 external clock/frame inputs, SPORT0 primary TX to
  the declared left-justified SDIN stream; SPORT1 has no clock source.

Zero MCLK selects the same wiring with the MCLK absent. The CLI rejects a
direct `--sport-tx-clock` or non-64 frame setting with this profile. The test
wires and B1 identity have no established physical OP-1 provenance.

The public [assembly fixture](../tests/fixtures/codec_guest/README.md) runs through
the normal LDR loader and Blackfin CPU. Guest instructions alone drive RESET,
TWI, codec configuration/readback, SIC/CEC and SPORT0/DMA4. The test driver
never writes guest MMIO, manually services peripherals, advances time or
injects interrupts. External MCLK pause/resume uses normal queued CPU events.
SPORT's existing deadline mechanism drives transmission and IDLE wakeups;
host polling does not drive peripheral progress.

The guest exercises slave selection, unsupported clock/format readback and
tri-state while configuring with PDN=1, then selects the supported master
configuration before clearing PDN. It keeps master and output mute requests
asserted throughout transmission. This deliberately separates native serial
capture from mute/output state. Native capture is taken from SPORT, not from
a simulated analog DAC. The SPORT abstraction serializes 32-bit stereo slots;
the fixture places 24 valid bits in each slot's MSBs and zeroes its low byte.
Electrical frame polarity, setup/hold and bit-edge alignment remain abstracted.

## Reproduction and checks

Build normally with `-DOP1_BUILD_HEADLESS_TESTS=ON`, then:

```sh
ctest --test-dir build -R 'op1-codec' --output-on-failure
python3 tests/codec_guest_integration.py \
  build/op1-codec-guest-test build/codec-guest build/op1emu
python3 tests/fixtures/codec_guest/build.py --as bfin-elf-as --ld bfin-elf-ld --check
```

The assembler/linker must be GNU Binutils 2.44, target `bfin-elf`, assembler
`-mcpu=bf524`; no C compiler/runtime is involved. CI builds stock GNU sources
whose archive hash is pinned in the workflow and rebuilds both public guests.

For a standalone normal frontend run, in a scratch directory with the normal
`gui/ui.json` available, use an erased/missing snapshot NAND and the fixture:

```sh
printf 'wait 30000\nquit\n' > /tmp/codec-test.input
op1emu missing.nand /absolute/path/to/tests/fixtures/codec_guest/diagnostic.ldr \
  --headless --deterministic --nand-snapshot --twi-speedup 1 \
  --sport-tx-mode transport --cs42l52-test-mclk 12288000 \
  --sport-tx-capture /absolute/path/to/capture \
  --max-cycles 10000000 --input-script /tmp/codec-test.input
```

No private firmware, boot, NAND or OTP content is required. The integration
runner also tests the shipping positional frontend and compares its PCM/CSV
with the CPU driver. Cases run twice without chaining and once with chaining;
PCM, serial timestamps, mailbox, codec transactions and TWI event JSON must
match byte for byte within each case. A deliberately short cycle budget must
fail while still writing diagnostics.

| Same guest image, declared external input | Required outcome |
|---|---|
| MCLK=12.288 MHz | PASS; 16 DMA-accepted words, one guest DMA ISR, 16 serialized words while mute requests remain asserted |
| MCLK absent | Control configuration/readback succeeds; guest watchdog wakes IDLE; STAL, four FIFO words accepted, DMA count12/address3010, no serialized words |
| RESET externally held low | Initial reset rejection followed by bounded CFAI at post-release probe; no SPORT/DMA submission or serialized output |
| MCLK interrupted for 50 µs | Queued interruption while TX active; transport freezes/resumes, exact same 16 samples, shifted timestamps, one DMA ISR. Records intentional early-MCLK-removal diagnostic, not valid analog shutdown. |

The successful run interrupts guest IDLE at IVG9. The guest stores `DMA9`,
observes DMA_DONE, acknowledges W1C, returns via RTI and stores `RTI9`. Ten words
have serialized at interrupt entry; six remain. A 26000-cycle/65 µs guest timer
guard drains them before SPORT is disabled, ahead of a completed underrun
replay. The model latches one underflow at the empty next slot, but captures
exactly the 16 valid new words. No repeated interrupt remains after acknowledgment.

The driver limits execution to ten million guest cycles, two million Run calls
and 20 seconds wall time. Failure JSON includes PC/exception state, CEC/SIC,
DMA address/count/configuration, SPORT status/buffers/acceptance/output counters,
codec state, guest mailbox and full bounded TWI/codec events. Each event source
has its own 2048-event capacity; `trace_dropped` is explicit and nonzero fails
the test. These public traces are independent of the private startup trace budget.

See [validation and output identities](cs42l52-validation.md).

## R4 evidence remains separate

The preserved MAIN hash, private boot/NAND context, explicit synthetic panel
reply, Port G raw storage, refusal and empty captures are unchanged. This test
does not reopen or bypass the guard. Physical reserved-encoding readback or a
different physical execution context remains unresolved. The remaining request
is still an observation bound to the exact image, boot context and silicon:
pre-store value/requested store, synchronized 16-bit read after the completed
store, 16-bit read immediately before admission, and intervening writes or
a different initialization path. No device access or flashing is involved.
