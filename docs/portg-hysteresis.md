# Port G hysteresis investigation

This checkpoint adds passive observation and a synthetic regression. It does
not change hysteresis readback, guest instructions, admission checks or board
clock inputs. The fixed private reference remains refused before codec I2C or
SPORT/DMA initialization. `legacy` remains the default SPORT mode.

Build with `OP1_BUILD_HEADLESS_TESTS=ON`, then run the public checks:

```sh
ctest --test-dir build -R 'op1-(memory-observation-test|startup-diagnostics)$' --output-on-failure
```

Private reproduction uses the saved input identities and launch arguments,
plus the history/checkpoint options in [startup diagnostics](startup-diagnostics.md).
Those inputs and raw traces stay outside this repository.

## Register contract

`PORTG_HYSTERESIS` is a 16-bit system MMR at `0xFFC03244`, distinct from Port G
GPIO data, direction and interrupt registers. The
[ADI BF52x Hardware Reference rev. 1.2](https://www.analog.com/media/en/dsp-documentation/processor-manuals/ADSP-BF52x_hwr_rev1.2.pdf),
9-24/9-25, Figure 9-6 and Table A-9, specifies reset `0000` and two-bit fields:

| Bits | Pins |
|---|---|
| 1:0 | PG0 |
| 3:2 | PG1 through PG4 |
| 5:4 | PG5 through PG6 |
| 7:6 | PG7 through PG8 |
| 9:8 | PG9 |
| 11:10 | PG10 |
| 13:12 | PG11 through PG13 |
| 15:14 | PG14 through PG15 |

Each field's `00` disables hysteresis, `01` enables it, and `1x` is reserved.
Thus setting bit 3 from reset writes `10` to the PG1..PG4 group; it does not
mean "enable hysteresis on PG3." The full two-bit field appears in Figure 9-6;
its upper bit is not separately specified as a read-zero bit. The manual's
reserved-bit preservation guidance does not define reserved-encoding readback.
Neither forcing zero nor applying `0x5555` is justified by that text.

Appendix A-3 requires access width to match the MMR's width. The reference uses
aligned 16-bit loads and a low-half 16-bit store. The
[BF522/BF524/BF526(C) anomaly list, revision H](https://www.analog.com/media/en/dsp-documentation/integrated-circuit-anomalies/ADSP-BF522-BF524-BF526-anomaly.pdf)
includes 05000122 for high-half register stores to 16-bit MMRs; this sequence
uses the low half. Its hysteresis mention under 05000265 concerns external
SPORT clock noise, not register readback. No applicable readback exception was
found in this list. The emulator advertises BF524 revision 0.2; the intended
physical device's silicon revision was not independently verified here.

## Storage and dispatch evidence

The current PORT_MUX region is generic zero-initialized byte storage, not a
complete pin-control model. Offset `0x44` selects the correct bytes. MMR pages
are excluded from direct guest-memory access; the disputed access reaches
device dispatch. Guest attended accesses, ordinary host accesses, and loader
bulk writes have the same little-endian lane behavior.

The synthetic regression executes the standard Blackfin sequence
`R0 = W[P1](X); BITSET(R0,3); W[P1] = R0.L; RTS` through normal DXE loading and
CPU execution. From zero, it reads zero and stores eight. It also checks host
byte, halfword, word and bulk access mechanics, upper-lane/padding preservation,
neighboring registers, origin scopes and raw-pointer exposure. Those extra
widths are emulator API tests; they do not authorize byte, 32-bit or padding
accesses to this physical 16-bit MMR.

There is no demonstrated dispatch, sign-extension, endian or lane defect in
the actual aligned 16-bit pattern. Raw storage retention of eight is an emulator
observation, not a qualified prediction for a reserved hardware encoding.

## Complete reference history

The unchanged private image and matching inputs were rerun with the same explicit
synthetic panel response, transport, snapshot storage, guest/wall bounds and no
forced external SPORT clock. Original captures remain separate. Exact image,
source/function-byte matches, instruction addresses, commands and traces are
recorded privately.

Observed order:

1. Immediately after CPU construction: zero, consistent with the constructor's
   storage initialization and the documented reset value.
2. Before and after the one DXE load: zero. No loader block overlaps this MMR;
   no INIT record is present or skipped.
3. One stock initializer reads zero, sets bit 3 in a CPU register and requests
   a 16-bit write of eight. Effective storage changes `0000` to `0008`.
4. The private admission snapshot reads eight and stores the 16-bit value in RAM.
5. At the run boundary immediately before the predicate's RAM load/comparison,
   that member and the MMR both contain eight. Admission compares the complete
   unsigned 16-bit member with zero, without a mask, and publishes refusal.
6. The stopped host snapshot reads the same storage. No intervening loader or
   other host write, later clear, raw pointer exposure, or history omission occurs.

The expected zero is a literal private admission-profile requirement, also
printed by its failure page. A measured pre-admission zero was not recovered
from the inspected source context. Reset zero alone does not establish the
value after initialization. An earlier retained value alone cannot explain
zero after this executed OR/set-bit operation under ordinary storage semantics.

The normal MAIN path executes the image's stock-derived entry and private
bootstrap; it does not replay physical TE-BOOT or reproduce all retained board
state. The preparation recipe explicitly described that ancestry as a context
to validate. One initializer write and one admission snapshot occur in this
reference; there is no observed duplicate or later clear. The physical order
has not been established by a corresponding register trace.

The historical hardware note names the matching candidate package hash, but
its original photograph, installed-image receipt and register transition trace
are unavailable in the inspected materials. It is a documented association,
not independent verification of exact executed MAIN bytes. If its later outcome
came from those exact bytes, control flow would imply that admission passed;
it would still not establish the value immediately after the earlier write or
why it differed. Its GPIO input-enable field is not a hysteresis reading.

## Remaining observation and correction boundary

No hardware correction is introduced from this evidence. To distinguish the
remaining explanations, obtain a trusted existing trace or a separately
authorized observation binding the exact MAIN, boot path and silicon revision
to: the 16-bit read before the set-bit operation, requested store value, 16-bit
read after the store has completed, and value immediately before admission.
Capture any intervening writes and whether the initializer executed. No physical
flashing is part of this session.

An after-write zero would support a reserved-encoding readback behavior for that
device/context. Eight followed by a later clear would support an initialization
or ordering difference. Absence of the write would identify a different path.
A later result label alone cannot distinguish them. A firmware change to either
the shared hysteresis field or its admission guard requires separate evaluation.

Codec reset/clock routing is still a later dependency. Empty serialized PCM does
not establish codec mute, analog output state, absent physical clocks or agreement
with the later silent-transmission failure reported on hardware.
