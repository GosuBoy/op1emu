# Public sustained streaming guest

`normal.ldr`, `delay.ldr`, `premature.ldr` are three synthetic BF524 programs
with the same code and one compile-time case immediate. All buffers are filled
and refilled by guest instructions. There is no firmware, WAD, Doom code,
vendor runtime or C compiler dependency.

GNU Binutils **2.44**, target `bfin-elf`, assembler `-mcpu=bf524`:

```sh
python3 tests/fixtures/stream_guest/build.py --as bfin-elf-as --ld bfin-elf-ld --check
```

CI builds stock hash-pinned GNU sources and verifies all three images, source
hashes and exported symbols against `manifest.json`. The linker reserves
4096 bytes at `ffa00100`; each checksummed FIRST|FINAL LDR contains 2308 bytes
and uses the normal direct DXE loader. `codec.inc` and `codec-macros.inc` are
the public finite codec guest's setup, independently of private R4.

The guest configures the selected CS42L52 test component through GPIO RESET
and TWI, then SPORT4401/021f and DMA4/PMAP4000. DMA uses finite 32-bit-bit,
SYNC1, DI_EN work units (`00a9`), count 128, modify4. The same normal image is
used for positive MCLK, pause/resume and stopped-clock watchdog tests.

Two 512-byte buffers at 3000/3200 hold64 interleaved stereo frames each. The
consumer submits256 work units; each slot is produced 128 times, 127 reuses
after its initial fill. The signed 16-bit bit pattern for block b0..255, channel
c0..1 and frame f0..63 is `((b+1)<<7)|(c<<6)|f`, packed as `uint16 << 16`.
Its 16384 stereo frames represent 341.333333 ms at the declared48 kHz rate.

Producer interface (exported labels in the symbol manifest):

- `produce`: R0=block generation. Requires the selected slot FREE, marks
  FILLING, fills it, executes SSYNC and publishes READY with its generation.
- `fill_words`: R0=synthetic signed 16-bit frame 0 tag, P1=packed destination.
  Produces64 L/R frames. A future mixer adapter can replace this function:
  render64 signed 16-bit stereo frames into owned staging and pack each component
  into the destination. Preserve P2 (owner pointer), P4/P5 and caller context;
  R0..R3, P1 and flags are scratch. A C adapter needs an explicit ABI wrapper.
- `start_dma`: R1=block. Requires READY/generation match and DMA status0,
  publishes IN_DMA and restarts the stopped work unit. DMAEN stays1 across
  restarts, preventing a starved SPORT's PIO service request from claiming
  the shared DMA interrupt. The ISR alone acknowledges DONE.
- Main processes the ISR completion and releases FREE. Six previously
  accepted SPORT words may still serialize while that RAM is refilled.

The addresses above are standalone fixture reservations, not native Doom
linker placements. A mixer integration needs linker-owned storage and verified
ownership, cache publication and ABI. It must explicitly configure48000 or
provide another evidenced rate/resampling path; the recovered44.1 kHz legacy
capture is not relabeled48 kHz.

The guest sleeps in IDLE, handles IVG9 DMA completion with W1C/RTI and uses
IVG6 for a4 ms work watchdog, 2 ms delayed producer and 65 us final drain guard.
SYNC1 leaves at most six SPORT words: 6*32/3072000=62.5 us. The65 us guard and
bounded instruction overhead stop before a completed replay word; the empty
next slot latches one explicitly accounted-for shutdown underflow. SPORT is
stopped before DMA ownership is released.

`delay.ldr` pauses the guest producer of block 10 for 2 ms while block 9 transfers.
This finite-restart mode starves SPORT:59 marked repeated words complete,
followed by a detected LRCLK phase slip. `premature.ldr` tri-states the codec
serial pins, verifies eight words already read (four in SPORT, four in DMA),
overwrites the owned buffer with tag 5000 and resumes clocks. The prefetched
eight survive, and exactly 120 unread components are deliberately corrupted.

Mailbox2000 contains 64 u32 diagnostics. Words0..39 retain codec diagnostic
positions. Words40..63 are case, processed/submitted/generated, owners0/1,
generations0/1, productions 0/1, event count/drop count, reserved, delay flag,
premature prefix count, watchdog flag, timer purpose/flag, reserved and the
last processed block. The guest ledger at 10000 contains up to 2048 records of
16 u32s: kind, block, CYCLES, DMA address/count/status, SPORT status, packed
owners, saved RETI/IPEND, submitted/generated, slot generations, IRQ-after/
before-ack. Event kinds1..11 are fill, ready, submit, ISR, release, delay begin/
end, premature overwrite, timer, drain and stop. Overflow is explicit and fails.

See [streaming contract, tests and limitations](../../../docs/streaming-audio.md)
and [validation identities](../../../docs/streaming-validation.md).
