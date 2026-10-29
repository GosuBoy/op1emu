# SPORT transmit transport, milestone 1

`--sport-tx-mode transport` enables a bounded digital TX model. PIO and DMA
enter the same FIFO; only completed serial words reach native capture. The
existing `legacy` mode remains the default for established stock-firmware
runs. Transport mode currently provides native capture, not host playback.

## Evidence and scope

Inspection at integration checkpoint `a2c9bfd` found two disconnected paths:
PIO only queued words, whereas DMA called the audio callback directly at a
fixed 44.1 kHz. Neither required an external clock or frame sync. SPORT had no
CPU service deadline, and DMA4 completion was not bound to the SIC.

Available custom-firmware initialization selects SPORT0 (`0xffc00800`), DMA4
(`0xffc00d00`, PMAP `0x4000`), TCR1 `0x4401`, TCR2 `0x021f`: external clock,
external required frame sync, 32-bit primary stereo. This is firmware routing
evidence, not a measured OP-1 clock waveform. Private source, artifacts, stock
firmware and OTP data are not incorporated here.

Sources consulted:

- [ADI BF52x Hardware Reference, rev. 1.2](https://www.analog.com/media/en/dsp-documentation/processor-manuals/ADSP-BF52x_hwr_rev1.2.pdf):
  chapter 24, especially configuration (24-50), TX FIFO (24-61), status
  (24-66), clock/FS dividers (24-67/68); chapter 5 for SIC routing; chapter 6
  for DMA mapping, buffering and completion. Section numbers refer to rev.
  1.2, not rev. 1.0. The full PDF endpoint could not be downloaded in this
  environment; its indexed excerpts were available. Consequently the
  following cross-family checks are disclosed rather than presented as a
  complete fresh audit of that PDF.
- [ADI's Linux SPORT definitions, Linux v4.16](https://github.com/torvalds/linux/blob/v4.16/arch/blackfin/include/uapi/asm/bfin_sport.h)
  and [MMR layout](https://github.com/torvalds/linux/blob/v4.16/arch/blackfin/include/asm/bfin_sport.h):
  TXSE is bit 8; TSFSE is bit 9. The legacy emulator reversed them. The new
  transport decodes the documented positions without changing legacy mode.
- [ADI BF527 SPORT clock description](https://wiki.analog.com/resources/eval/sdp/sdp-b/peripherals/sport):
  internal TSCLK = SCLK / (2 * (TCLKDIV + 1)), configurable framing and
  3..32-bit words.
- [ADI BF533 HRM rev. 3.6, reproduced text](https://manuals.plus/m/bf6618c2ab77e3f9ef71f6dea39b6d06daa96a4e955d24aed855618e152c55a3):
  figure 12-1 and sections 12-23..29, 12-44 cross-check the shared SPORT
  architecture, 8x16/4x32 FIFO, holding/shift registers, W1C status, PIO/DMA
  interrupt selection and underrun retransmission. Its status-register diagram
  and dedicated status description specify W1C; some TX/error prose says
  errors clear only on disable. The model follows the register description.
  Applying these details to BF524 is a cross-family model assumption until
  checked against a complete BF524 reference/hardware trace.

The I2C addresses currently attached in `cpu.cpp`, including `0x4a`, are dummy
peripherals. An ACK or matching one-byte response does not establish a codec
identity, reset state, PLL lock, or clock output. No chip-specific codec
behavior, GPIO-to-codec wiring, or automatic clock generation from I2C has
been added. Reports of unchanged DMA position/full TX are evidence of a
stall to investigate, not proof that missing clocks caused it.

## Implemented contract

| Area | Transport behavior |
|---|---|
| Configuration | Normal data, one primary pin, 3..32-bit words, mono or stereo. TCR1/TCR2, TCLKDIV/TFSDIV and MCMC readback are implemented for this subset. |
| Acceptance | PIO writes and DMA use one bounded FIFO: eight words up to 16 bits, four above 16 bits. DMA moves complete memory elements and peripheral words, including pairs of 16-bit words in a 32-bit DMA element. |
| Staging | One holding word and one shifting word are separate from the FIFO. FIFO-to-hold movement requires a serial clock edge; serialization requires the selected FS when TFSR is set. |
| No clock | A cold FIFO can fill, then DMA returns zero and keeps its current address/count. No serial word completes and TUVF is not invented from elapsed wall time. |
| No FS | With clocks present, a holding word can stage. No new framed word starts. A word already shifting can finish. |
| Stop/restart | Clock stop freezes partial bits and oscillator phase. Restart resumes them; no credit accrues during the stopped interval. |
| Transmission | An accepted word completes only after its serial word clocks. DMA_DONE describes acceptance, and may precede every serial completion. SPORT drains after DMA stops. |
| Status | TXF describes the FIFO, TXHRE is 1 when the holding register is empty. Full PIO writes are discarded and set TOVF. Empty active framing sets TUVF. Error bits are sticky/W1C; zero does not acknowledge them. |
| Underrun | External or data-independent internal FS repeats the last known word, marked `repeated=1`. Before any known word, slots are marked `valid=0`; no arbitrary zero is written to PCM. Data-dependent internal FS suppresses empty frames. |
| Interrupts | PIO TX service is a level while enabled with FIFO space, suppressed by mapped TX DMAEN. SPORT0 error is SIC8 and TX/DMA4 shares SIC17; SPORT1 uses SIC9/19. DMA and PIO levels are ORed; DMA_DONE W1C cannot erase a PIO request. |
| Disable/reset | TSPEN=0 aborts partial serialization, clears FIFO/holding/shift/error state and service/error levels, retaining configuration. Aggregate diagnostic counters remain. A new device resets counters and registers (STAT=0x40). DMA disable alone preserves peripheral buffering. |

The exact FIFO-to-hold handoff, initial FS phase, and disable flushing are
explicit modeling choices. The first modeled data slot begins on serial edge
1 after enable; completion is one word length later. Slots recur at the
configured FS period, halved for external stereo LRCLK. Internal TFSDIV gives
clocks per word/half-frame. Clock calculations retain rational divider
precision and fractional phase; CPU time still uses the existing 400 MHz
packet/IDLE model and assumed 100 MHz SCLK, not PLL timing.

Early/late FS, electrical polarity, pin mux connectivity, startup synchronizer
latency and individual pin waveforms are abstracted. `TLSBIT` is recorded in
capture; native word values are right-justified independent of wire bit order.
Secondary pins, companding, multichannel and invalid word lengths fail closed
(no DMA acceptance/serialization, `Snapshot.supported=false`). Changing
configuration while enabled likewise requires disable/re-enable before
progress resumes. These are outside the qualified subset, not invented
hardware error flags. Configure clock sources and dividers before enabling.

The DMA controller's separate four-element hardware FIFO, descriptor fetch
latency, SYNC-dependent memory-side vs peripheral-side completion timing,
cache coherence and arbitration remain outside this milestone. Existing DMA
counts describe words accepted by this peripheral model; they cannot be used
as exact BF524 memory-prefetch or wire-drain measurements. SPORT RX retains its
legacy model. This is not a full-duplex hardware qualification.

## Reproduce without firmware or a sound device

Build with the development guide's headless test options, then:

```sh
./build/op1-sport-transport-test
./build/op1-sport-transport-test --demo /tmp/sport-running > /tmp/sport-running.log
./build/op1-sport-transport-test --demo /tmp/sport-clockless --no-clock > /tmp/sport-clockless.log
./build/op1-sport-transport-test --demo /tmp/sport-repeat > /tmp/sport-repeat.log
cmp /tmp/sport-running.pcm /tmp/sport-repeat.pcm
cmp /tmp/sport-running.csv /tmp/sport-repeat.csv
```

The fixture uses the real `Emulator` MMIO dispatch, SPORT, DMA4 and SIC with
synthetic RAM. It is a host-driven MMIO fixture, not a guest executing an
OP-Doom image. The running schedule is: 0..100 us clock off, 100..200 us on,
200..400 us off, then on through the sixteenth word at 813 us, followed by
SPORT disable. Stimulus is exactly 1 MHz, 64 clocks per stereo frame; RAM
contains 16 32-bit values `0x12340001 + i*0x101`. No random input, NAND,
proprietary initialization, or host audio is involved.

Expected observations:

- Clockless: accepted=4, transmitted=0, DMA count=12/address=`0x1010`,
  FIFO=4, STAT=`0x48`; empty `.pcm` file.
- At 200 and 400 us: accepted=9, transmitted=3, DMA count=7; the stopped
  interval has no output timestamps. Partial serialization resumes afterward.
- At 813 us: accepted=16, transmitted=16, DMA count=0/address=`0x1040`.
  DMA_DONE was already asserted at 622 us. The next empty frame sets TUVF;
  disable aborts its pending replay, clears SPORT status to `0x40`, and leaves
  DMA_DONE pending for explicit DMA acknowledgment.
- Native PCM is 64 bytes, SHA256
  `9e412b21be559d5c47c41f35835d565a31a05dc458747b28d251c39ad2fa7f44`.
  Running CSV SHA256 is
  `3cb8b40c355b3d93712c04a9bcd5061b3a47b49cb4684d215b9c18951ac4ad9e`.

Stdout records emulated time, accepted/transmitted counters, DMA position and
count, FIFO/hold/shift occupancy, status and interrupt transitions. CSV records
completed slot time, index, word, length, stereo slot, bit order and validity/
repeat flags. PCM contains only known completed words, in little-endian
16-bit or 32-bit containers; use CSV for gaps/unknown slots. It has no WAV
header, host conversion, silence insertion or resampling. A counter can
include unknown clocked slots or repeats; it is not the number of distinct
DMA words consumed. Interrupt transition times are service observation times;
CSV completion times come from modeled clock edges.

CTest runs the six transport groups and both demonstrations, including PCM
file content checks. CPU event tests cover scheduling without DMA or MMIO
polling and the actual CPU SIC17 binding. Existing legacy audio regressions
remain active. CI uploads the synthetic running/clockless evidence from Linux
(profiling on/off) and macOS.

## Firmware run and next milestone

For a separately supplied image, transport mode is selectable without
bypassing guest initialization:

```sh
./build/op1emu --headless --deterministic --nand-snapshot \
  --sport-tx-mode transport --sport-tx-capture /tmp/firmware-tx NAND_IMAGE
```

That command supplies no external clock. Only for an explicitly synthetic
clock experiment, add `--sport-tx-clock 2822400 --sport-tx-frame-clocks 64`
(44.1 kHz stereo frames). This is test stimulus, not codec initialization or a
verified physical board rate. Legacy mode retains the former DMA audio
callback/host playback path and is not evidence of hardware-intended startup.

Available OP-Doom sources were inspected, but the precise executable images,
stock initialization context, pinned compiler/loader and privately archived
build inputs were unavailable locally. Hardware-intended OP-Doom startup was
therefore **not executed**, and neither codec setup nor physical OP-1 behavior
is validated. The next concrete milestone is to restore that pinned fixture
privately, establish the codec identity and clock/pin route from board evidence,
then run unchanged initialization with a documented digital codec/control
model and compare its first clock/FS/DMA observations with hardware.
