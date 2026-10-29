# 44.1 kHz and standalone guest mixer validation

Local validation: Apple silicon, Apple Clang 17 / LLVM 15, Release.
All **36 native CTests pass**, including both streaming rates, existing
frontend/provisioning/legacy tests and the prior finite SPORT/codec fixtures.
All nine public LDR images rebuild with stock GNU Binutils 2.44. The original
three 48 kHz streaming images retain their exact previous SHA256 identities.
The public observer's event JSON additionally records its selected limits;
the existing 48 kHz PCM/CSV identities remain unchanged.

## Public 44.1 kHz matrix

Each case runs twice without chaining and once with chaining, comparing the
complete PCM, CSV and JSON bytes. Normal, delayed and premature-reuse cases
also match the ordinary headless CLI capture. Results use the declared
11.2896 MHz MCLK, guest TWI configuration and derived 2.8224 MHz SCLK.

| Case | DMA IRQs | Fresh / repeated components | Incorrect fresh slots | Result |
|---|---:|---:|---:|---|
| Normal | 256 | 32768 / 0 | 0 | Complete |
| Delayed producer | 256 | 32768 / 43 | 31488 | Preserved intentional audio failure |
| Premature reuse | 256 | 32768 / 0 | 0 | Exactly 120 intentionally corrupt components; first 8 protected |
| Clock pause | 256 | 32768 / 0 | 0 | Complete, exact 500 µs gap |
| Permanent clock loss | 32 | 4134 / 0 | 0 | Guest 4 ms watchdog |
| Delayed producer, safe stop | 10 | 1280 / 42 | 0 | Guest `UNDR`, no further submissions |

The three ordinary complete cases account one final post-data underflow as
the drain timer expires. The legacy delayed case accounts its 43 replayed
components plus one partial repeat at final shutdown. The safe-stop case
preserves 42 complete replays and one partial repeat at stop. No replay is
deleted from native PCM. Buffers peak at four DMA plus six SPORT words.
At clock loss, DMA read 48/accepted 44/remaining 80/FIFO 4; SPORT has FIFO 4,
holding 1, shifting 1, transmitted 4134. Both passive stalled snapshots are
identical. No elapsed clock credit, extra IRQ or serialized output appears.

Public normal 44.1 kHz identities:

- Image: `3dbba60257ca99ec342e112eb3a3896b184e8c4fb1d6eb4b3907352c22adbabc`
- PCM (131072 bytes): `fc7e7b88a9eac4c0afa054a9c3b33a67d0fc9d58563ef35c985cffe42a7983b2`
- CSV: `91858c0d53aa2fc64258742796639a3e9fbbe4e53c0bf7376418dede81541c6a`

The PCM equals the 48 kHz pattern because the synthetic sample values are
unchanged; the 44.1 kHz timestamp cadence is independently checked. The
capture is not relabeled 48 kHz. All case identities and metrics are emitted
in `build/stream-guest-44100/identities.json` and `metrics.json`.

## Private guest-executed recovered mixer

Exact private source/revision pins, compiler binaries/source hashes, runtime
hashes, launch commands, linker maps and captures are retained in the private
fixture's build receipt and report. None are required by public CI. These
are newly rebuilt standalone images, not old V74/R4 executables.

| Case | Frames / fresh words | IRQs | Events (start/stop) | Render calls | Result |
|---|---:|---:|---:|---:|---|
| Audible schedule | 141312 / 282624 | 2208 | 13 (7/6) | 2208 | Complete, no replay |
| No-event control | 141312 / 282624 | 2208 | 0 (0/0) | 2208 | All samples zero |
| Clock pause | 141312 / 282624 | 2208 | 13 (7/6) | 2208 | Same PCM, exact 500 µs pause |
| Permanent clock loss | 2067 / 4134 | 32 | 1 (1/0) | 34 | Guest watchdog, pending data retained |
| Late producer | 640 / 1280 | 10 | 1 (1/0) | 10 | Diagnostic stop, 42 replayed words |

Every complete run spans exactly 141312/44100 = **3.204353741… seconds** of
sample data. The audible schedule reaches frame 125003, after 2.8 seconds.
Each slot has 1104 productions /1103 reuses. All fresh channel slots match;
there is no steady-state underrun or replay. Every value equals both an
independent mathematical oracle and a separately executed reference compiled
from the exact recovered mixer/decoder. The reference's 64- and 512-frame calls
produce the same complete output and exact event placement.

The guest event ledger records all 13 requested sample indices and parameters.
Separate unchained PC observations count actual function entries:

- `dmx_parse`: 4 (three valid generated inputs and one invalid-format control).
- `sfx_init`: 1; `sfx_start`: 7; `sfx_stop`: 6.
- `sfx_render`: 2220 segments across 2208 buffers, split at exact event indices.
- `mixer_produce`: 2208 calls from the guest assembly producer.

All 2208 DMA ISR records show IVG9, DONE before acknowledgment, clear DONE
after W1C, and the correct IDLE return address. Ownership/generation records
prove that rendering uses a released FILLING slot and that release follows
the ISR. Per-block DMA source-read and peripheral-acceptance digests match
the oracle; completion precedes the last six serialized words. Main context
does the decoding/mixing; the ISR acknowledges and records completion.

Audible first/last component completions: 4541080 / 3208883483 ns. Guest cycles:
1283554831. Stack paint high-water: 204 bytes (silent 152) within its guarded
65536-byte reservation; all eight guards survive. There is no linked heap or
allocator. These are functional emulator measurements, not a physical BF524
worst-case timing or stack guarantee.

Each of the five private cases runs twice unchained and once chained;
PCM, CSV and event JSON are byte-identical across repeats. A separate
execution-proof run adds PC observation. The full mathematical output and
64-/512-frame host references also match byte for byte. No trace capacity
overflows. Private capture identities for the audible case:

- Native PCM (1130496 bytes): `9d939cb7ae4b1e576869655e4ae77728e5fb0959cd67e43351c4e10c35c6f12c`
- CSV: `7d50dbc1d16b27097ee25f20532c7556baaec641f7bbd9f471dc4e88ad3d7e2c`
- Event JSON: `54488a06f5a96687a6f9794bf13d81c78504d49825f499b63dae67f47247d9a8`

The complete identities for all private cases are retained outside the
repository. [Reproduction and limits](doom-mixer-integration.md) describe the
separate private runner, compiler recipe, clock assumptions and remaining
full-game dependencies. Codec mute/output state remains separate from native
SPORT capture; no physical compatibility or analog-output result is claimed.
