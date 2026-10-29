# CS42L52 component validation

The independent digital component test uses only public synthetic inputs. It
preserves the earlier R4 refusal and does not qualify any physical OP-1 part,
wiring, oscillator, analog startup or output.

## Local validation

Apple Clang 17, LLVM 15, release build on Apple silicon: all 33 CTests passed,
including the prior transport, guest, startup, frontend, provisioning and
legacy regressions. The final guest independently rebuilds with GNU Binutils
2.44 (`bfin-elf`, `-mcpu=bf524`); no C compiler/runtime. CI rebuilds both guests
from the hash-pinned upstream GNU source archive.

The same 1700-byte LDR is used for every codec case. SHA256:

`0d9a25c40b969f756d503de0b8721f09b8a41deb21de46fff895b90cb916413b`

Twice without chaining and once with chaining, all four cases produce identical
PCM, CSV and complete event JSON within the case. The normal CLI with
`--cs42l52-test-mclk` and TWI speedup1 produces the same PCM/CSV as the driver,
with no directly supplied SPORT clock. The deliberately short cycle-budget run
fails and still exports PC/interrupt/DMA/SPORT/codec diagnostics.

| Case | Accepted / DMA ISR / serialized | Guest result |
|---|---|---|
| MCLK present | 16 / 1 / 16 | PASS; exact finite buffer |
| MCLK absent | 4 / 0 / 0 | STAL after guest watchdog, count12/address3010 |
| External reset held | 0 / 0 / 0 | CFAI before SPORT setup |
| 50 µs MCLK interruption | 16 / 1 / 16 | PASS; identical samples, deferred serial timestamps |

The known buffer is `0x12340000+i*0x10100`, i=0..15, serialized alternately
left/right into 64 native little-endian bytes. All low bytes are zero. Both
successful cases retain master-mute3 and output-mute15; native PCM is serial
transport evidence, not analog codec output.

## Executed guest evidence

The committed symbol manifest binds entry `ffa00100`, codec setup `ffa0045c`,
configured point `ffa006d4`, DMA ISR `ffa00370`, timer ISR `ffa003f4`, and the
post-IDLE resume addresses `ffa0029c` / `ffa002d8` to this image.

In the positive run:

- Guest reset release is at cycle418388; reset-time TWI rejection is observed.
  Guest reads ID=e3, reset power=01, clock=a0 and preserved register32=3b.
- The guest issues the 4.11 commands and verifies documented controls; the
  model records initialization complete. Global PDN clears at 4267270 ns,
  producing 3072000 Hz SCLK /48000 Hz LRCK with the mute requests asserted.
- SPORT starts at cycle1743011. DMA ISR entry is cycle1784936; W1C acknowledgment
  is cycle1784972. DMA count0/address3040 and IRQ1->0 are stored by guest code.
- The ISR's saved RETI is `ffa0029c`, proving interruption of guest IDLE. The
  guest writes DMA9, returns with RTI and writes RTI9; exactly one ISR executes.
  Ten words are serialized then, with six still buffered.
- First serial completion is 4368263 ns; final word15 completes at 4524513 ns,
  after DMA acknowledgment. Guest core timer interrupts the drain IDLE, then
  SPORT stops with 16 accepted/serialized, no overflow, empty buffers and no
  repeated interrupt. The empty next slot latches one underflow before stop;
  no repeated or invalid word completes.
- All 151 codec events and 149 TWI events are retained; zero truncation. Each
  source has an independent 2048-event bound with explicit dropped count.

The 50 µs external-input interruption is scheduled through normal CPU events;
no host polling, peripheral servicing or interrupt injection drives progress.
Missing MCLK still permits the full guest control-port sequence/readback. Held
reset instead takes the bounded control-error path. Unit coverage also checks
reset defaults/reassertion, out-of-contract short pulses, MAP fragmentation and
auto-increment, hidden-command order, read-only ID, unsupported accesses,
master/slave, tri-state, mute independence, unsupported rates/formats and the
100 µs power-down/MCLK-removal diagnostic boundary.

## Output identities

SHA256 of the generated artifacts (also in CI `codec-guest/identities.json`):

### clock

- `csv_sha256`: `b1739c3932f563e354c759e24e1fb625f197ed48b4eec8147cc1f59dadc606f9`
- `events_sha256`: `054553f92e830a5819cd224c2ad9a9a442de42ce79e9489de0f42f9b22b6d078`
- `pcm_sha256`: `0079b6aa70cb180f6f1ae643e659cccfd4cf4b65d07135dd21f8c7a112b6228f`

### no-mclk

- `csv_sha256`: `a5c670d61d4cee822b3a6e8db7a42bf4058fb634f0855f08a1c8e555003fed23`
- `events_sha256`: `fde35edc911f6f674ec2a305bef9aec892b7fdedd0e147dadde010a40c059453`
- `pcm_sha256`: `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`

### reset-held

- `csv_sha256`: `a5c670d61d4cee822b3a6e8db7a42bf4058fb634f0855f08a1c8e555003fed23`
- `events_sha256`: `516204a016b82a60028a03fb0bcec7eba7b84904d1aac1d5f47242a1d13872d5`
- `pcm_sha256`: `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`

### pause

- `csv_sha256`: `c648b1d8cda492e4499a4cf13dd7397fee41feb529baf816600964de043495a6`
- `events_sha256`: `73f418747cb9df7337798a5285cc4e0ceb69d6bc42ec4d6b15872f56818748d6`
- `pcm_sha256`: `0079b6aa70cb180f6f1ae643e659cccfd4cf4b65d07135dd21f8c7a112b6228f`

## CI and remaining limits

The implementation CI result is recorded after the branch run completes.
Native CTest configurations include Linux with/without profiling and Apple
silicon; the existing GNU fixture rebuild, Python tooling, libc++, frontend and
legacy jobs remain in place. Only public synthetic captures are uploaded.

See the [component contract](cs42l52-test.md) for explicitly unsupported
features, power-down clock waveform scope, reset/wiring assumptions and
reproduction commands. Physical codec identity/reset/MCLK, electrical timing,
analog readiness/output and R4 reserved-encoding readback remain unqualified.
No private firmware or traces were added to this validation or CI.
