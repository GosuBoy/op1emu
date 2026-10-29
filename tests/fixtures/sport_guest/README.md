# Guest-executed SPORT diagnostic

`diagnostic.ldr` is a public, synthetic BF524 program: GNU assembly only, no
C runtime, vendor firmware, OTP, NAND content, OP-Doom source or private assets.
The single checksummed FIRST|FINAL LDR block loads at `0xffa00100` through
`LDRParser` and `LoadDXE`, the same direct-loading path as the positional LDR
frontend. This tests direct DXE execution, not the NAND boot ROM. The source
initializes scratch stack `0xffb01000`, mailbox `0x2000`, and samples `0x3000`.

## Build and identity

Assembler and linker: **GNU Binutils 2.44, target bfin-elf**. No C compiler is
used. The committed image is 868 bytes, SHA256:

`ddaa636d2deb5cb91c1a05173bea3fa111e74a6e62984d6c781cd15f09b02789`

Rebuild using a suitable Binutils installation:

```sh
python3 tests/fixtures/sport_guest/build.py \
  --as bfin-elf-as --ld bfin-elf-ld --check
```

The script invokes `as -mcpu=bf524 --fatal-warnings` and
`ld -T tests/fixtures/sport_guest/link.ld`, extracts the sole allocated ELF
section, and constructs the LDR header using only Python's standard library.
`--check` compares every image byte, source hash and exported symbol with the
committed manifest. Omit `--check` only when deliberately updating the fixture.
Temporary ELF/object files are not inputs to execution.

To build these tools from the public release, as the independent CI job does:

```sh
curl --fail --location -o /tmp/binutils-2.44.tar.xz \
  https://ftp.gnu.org/gnu/binutils/binutils-2.44.tar.xz
echo 'ce2017e059d63e67ddb9240e9d4ec49c2893605035cd60e92ad53177f4377237  /tmp/binutils-2.44.tar.xz' | sha256sum -c -
tar -xf /tmp/binutils-2.44.tar.xz -C /tmp
mkdir /tmp/binutils-build
cd /tmp/binutils-build
/tmp/binutils-2.44/configure --target=bfin-elf --disable-nls \
  --disable-werror --disable-gprofng --disable-gold --with-system-zlib
make -j2 MAKEINFO=true all-gas all-ld
```

Then, from the repository, pass `/tmp/binutils-build/gas/as-new` and
`/tmp/binutils-build/ld/ld-new` to the script. macOS can use `shasum -a 256`
instead of `sha256sum`. Initial native macOS tool executable SHA256 identities:
assembler `c9265d32722ce95a429df8fe7757098a1d396eadc1f36d7eaee615a763baf3b8`,
linker `46decb3d65de2394ebb11812db5401f1f13124906905c8ea9afd44247b668634`.
The stock GNU source rebuild check is the portable reproduction criterion;
host executable hashes naturally differ.

## Guest sequence and assumptions

1. Leave reset via RAISE15/RTI, run in IVG15, push RETI to permit higher
   priority interrupts, install EVT9 (DMA), EVT6 (core timer), EVT3 (exception).
   Set CEC IMASK to enable IVG15/9/6.
2. Clear the mailbox and **execute stores** for sixteen 32-bit samples
   `0x12340001 + i*0x101`. Preserve R5=`0x76543210` across interrupts.
3. Enable only SIC17, map it to IVG9 using SIC_IAR2=`0x20`, and select only
   SIC17 for peripheral IDLE wakeup. Other board sources remain masked.
4. Arm the one-shot core timer for 800000 cycles with scale zero, using the
   existing 400 MHz packet/IDLE time model (2 ms watchdog).
5. Disable SPORT0, clear MCMC1/2, set TCR2=`0x021f` (32-bit primary stereo).
   Disable DMA4, clear old DONE/ERR, set start=`0x3000`, X_COUNT=16,
   X_MODIFY=4, PMAP=`0x4000`, CONFIG=`0x0089` (finite stop, DMAEN,
   WDSIZE32, DI_EN), then TCR1=`0x4401` and SSYNC.
6. Wait using IDLE. On DMA_DONE, the actual IVG9 guest ISR saves/restores
   ASTAT and its working registers, records RETI/IPEND/current DMA/SPORT,
   acknowledges DONE by W1C, and returns with RTI. Main records `RTI9`.
7. Rearm the core timer for 128400 cycles (321 us), continue IDLE while SPORT
   drains, then disable SPORT, record `PASS` and jump to the normal LDR return
   sentinel `0x08000000`. Without a clock, the original timer ISR wakes main,
   which records `STAL` and returns with DMA/SPORT still enabled for inspection.

SPORT0/DMA4 and the register values reuse the established custom-firmware
selection; this independently authored program does not reproduce that
firmware's complete board initialization. The explicit test stimulus is
**1 MHz external TSCLK with 64 clocks per stereo frame**, present from startup
in the positive case. The negative case has no clock or FS source. The same
image runs in both cases; it neither detects the test mode nor forces clocks.
The fixed drain interval is valid only for this rate, word size and finite
buffer. It is not a general firmware drain algorithm.

The model can start an underrun replay after the final word. The guard ends
after all sixteen new words complete and before a replay can complete; guest
disable clears the pending replay. Final pre-disable STAT=`0x50` and one
underflow are expected. Native capture contains no repeated or invalid words.
No codec, I2C identity, reset/PLL state, pin mux or physical route is qualified.

## Run

Enable `OP1_BUILD_HEADLESS_TESTS` as in the development guide:

```sh
cmake --build build
ctest --test-dir build -R '^op1-sport-guest$' --output-on-failure
python3 tests/sport_guest_integration.py ./build/op1-sport-guest-test \
  ./build/sport-guest ./build/op1emu
```

The integration test verifies fixture/source identities, runs each mode twice
with the frontend's unchained CPU policy and once with chaining, and compares
PCM, serial CSV and guest JSON **byte for byte**. Different numbers of host
Run calls must not change output, guest timings, interrupt counts or state.
It also runs both modes through the shipping `op1emu NAND LDR` command and
requires identical PCM/CSV. The missing NAND is read as erased in snapshot
mode, with temporary synthetic OTP; no proprietary boot input is used.

The C++ driver only configures the transport and synthetic clock, loads the
image, and calls `cpu.Run()`. It does not populate the samples, configure
MMIO, poll DMA/SPORT to cause progress, advance time, service peripherals or
inject an interrupt. All such initialization and interrupt acknowledgment
comes from guest instructions. The capture callback observes completed words.

Standalone runs, including failure diagnostics:

```sh
./build/op1-sport-guest-test tests/fixtures/sport_guest/diagnostic.ldr /tmp/guest clock
./build/op1-sport-guest-test tests/fixtures/sport_guest/diagnostic.ldr /tmp/guest-off no-clock
./build/op1-sport-guest-test tests/fixtures/sport_guest/diagnostic.ldr /tmp/guest-limited clock --max-cycles 100
```

The last command must fail and emit a state dump. Normal limits are 2 million
guest cycles (5 ms), one million Run calls, and 20 s between-call wall limit;
the Python parent additionally limits each process to 30 s and CTest to 180 s.
JSON records PC, SEQSTAT/RETX, CEC IMASK/IPEND/ILAT, SIC routing/status, DMA
current address/count/config/IRQ, SPORT config/status/FIFO/holding/shifter,
accepted/transmitted/error counters, the guest mailbox and the sample buffer.
Failure JSON is also printed to stderr. CI uploads these artifacts on failure.

## Mailbox and evidence

All fields are little-endian u32 at `0x2000 + offset`; unused fields remain
zero. CYCLES timestamps are the existing CPU model's **basic-block entry**
counts, not instruction/pipeline latency measurements. Explicit branches
separate the startup and acknowledgment timestamp blocks.

| Offset | Meaning |
|---|---|
| 0, 4 | signature (`PASS`=0x50415353, `STAL`=0x5354414c, `EXCP`, `FAIL`), phase (1 initialization, 2 DMA wait, 3 drain) |
| 8, 12 | DMA ISR entries, timer ISR entries |
| 16, 20, 24 | after-RTI tag `0x52544939`, sample count, IDLE entries |
| 28, 32, 36 | start, DMA ISR entry, acknowledged CYCLES |
| 40, 44, 48 | drain guard start, timer ISR entry, finish CYCLES |
| 52, 56 | DMA count/address inside DMA ISR |
| 60, 64 | DMA IRQ before/after guest W1C |
| 68, 72, 76 | SPORT STAT, CEC IPEND, saved RETI inside DMA ISR |
| 80, 84, 88 | final DMA count/address and SPORT STAT before disable |
| 92, 96, 100 | final CEC ILAT/IPEND and preserved R5 |
| 104, 108 | exception SEQSTAT/RETX |
| 112, 116, 120 | timer RETI, ISR tag `0x444d4139`, phase observed by timer |
| 124 | reserved |

The symbol manifest identifies the ISR and both instructions following IDLE.
Tests require DMA RETI=`idle_dma_resume` and timer RETI=`idle_drain_resume`
(clocked) or `idle_dma_resume` (clockless). This proves an interrupt of the
guest IDLE path. The ISR tag, one entry, IPEND bit9, DONE 1→0, after-RTI tag,
preserved canary, and cleared final ILAT/IPEND bit9 establish ISR execution,
acknowledgment, correct return and no subsequent delivery during drain.

See [validation results](../../../docs/sport-guest-validation.md) for measured
event times, capture identities, CI links and remaining private dependencies.

The streaming milestone qualifies a separate four-word controller FIFO for
32-bit finite TX DMA. With SYNC0, DONE now means the final source read; RUN
can remain set while that FIFO drains. The current image uses a derived
321 us drain guard for four DMA plus six SPORT words. No-clock DMA
reads eight words (four accepted by SPORT plus four held in DMA), leaving
count8/address3020 in this 16-word test. See [DMA qualification and streaming](../../../docs/streaming-audio.md).
