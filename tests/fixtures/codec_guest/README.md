# Public guest-issued codec/clock diagnostic

This independent BF524 assembly fixture uses the normal direct LDR loading
path at `0xffa00100`. It needs no proprietary firmware, NAND, OTP, vendor runtime
or OP-Doom materials. Stack is `0xffb01000`, mailbox `0x2000`, samples `0x3000`.
It extends the public SPORT guest's interrupt/drain exercise with guest GPIO
reset, TWI control-port transactions and supported codec readback.

Assembler and linker: GNU Binutils **2.44**, target `bfin-elf`, `-mcpu=bf524`.
No C compiler/runtime is used. `build.py` extracts only the linked `.text` from
ELF32/EM_BLACKFIN into one checksummed FIRST|FINAL LDR block. Source, linker,
build-script and image hashes plus exported execution addresses are recorded
in [manifest.json](manifest.json). The image is 1700 bytes, SHA256:

`0d9a25c40b969f756d503de0b8721f09b8a41deb21de46fff895b90cb916413b`

```sh
python3 tests/fixtures/codec_guest/build.py --as bfin-elf-as --ld bfin-elf-ld --check
```

Use the pinned GNU source build commands in `.github/workflows/ci.yml` when the
tools are unavailable. Omit `--check` to deliberately regenerate the committed
image and manifest after editing sources.

The guest drives the explicit test profile's PG12 RESET wire, waits at least
400000 modeled CCLK cycles, probes B1 ID/defaults, performs DS680F2 4.11
initialization preserving register32, and verifies the documented control-register readbacks. Hidden commands
are write-only in this bounded model; their acknowledgment/order is checked.
TWI uses the controller's 10 MHz reference with CLKHI12+CLKLOW13, speedup1:
400 kHz. Reads follow the specified MAP-write/STOP/read sequence.

Guest codec state `05=20,06=80,07=00,02=00` derives 3.072 MHz SCLK and 48 kHz
LRCK from the declared 12.288 MHz MCLK. SPORT `4401/021f` and DMA4 `0089`,
PMAP4000, count16, modify4 serialize samples `0x12340000+i*0x10100` for i0..15.
Each native little-endian 32-bit container holds 24 MSB payload bits and eight
zero pad bits. Sixteen words are eight stereo frames, not 16 stereo frames.

IVG15 main enables IVG9 DMA and IVG6 core timer, enters IDLE, checks the guest
ISR's acknowledgment/RTI, waits a 26000-cycle drain guard and returns normally
to `0x08000000`. The no-MCLK image returns STAL after its 800000-cycle guest
watchdog. An externally held RESET returns CFAI before codec/SPORT setup.

Mailbox words0..30 retain the SPORT fixture's diagnostic ABI. Word31 stores the
last TWI status; words32..39 store ID, reset power/clock readbacks, INIT tag,
preserved register32, reset-time rejection status, release and configuration
cycle timestamps. The fixture clears 48 words. `manifest.json` identifies
the DMA/timer ISR and post-IDLE return addresses checked by the integration test.

See [component contract, assumptions, limits and commands](../../../docs/cs42l52-test.md)
and [validation identities](../../../docs/cs42l52-validation.md).
