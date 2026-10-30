# Synthetic ROM INIT handoff

This milestone tests the shared ROM INIT/callback protocol with owned public
code. It preserves the readiness checkpoint `ac95c970d2db47604b92a3c86ac0310e0542eb85`,
the direct loader, existing firmware images, peripheral defaults, legacy audio
selection and the R4/Port G refusal. It does not reproduce a physical OP-1 boot.

## Supported contract

The [ADI BF52x Hardware Reference rev. 1.2](https://www.analog.com/media/en/dsp-documentation/processor-manuals/ADSP-BF52x_hwr_rev1.2.pdf)
chapter 17 documents multi-block and zero-payload INIT, the R0 boot-data calling
convention, callbacks and memory boot. The test executes the repository's existing
BF526 0.2 ROM bytes. Their raw image SHA256 is
`9fe4f939d4bf098fbfbc774e67241bd3ddfd6a410005cf40927e99cc710f871d`.
The available matching BF526 V02 reference identifies that implementation; it
is not evidence of the owner's BF524 ROM, silicon or installed boot inputs.

The normal direct loader installs a guest bootstrap and nested memory stream.
Guest instructions select a scratchpad stack, mask interrupts and call
`BFROM_MEMBOOT` at its public API vector. Actual ROM instructions then copy INIT
into L1, zero the mailbox, call INIT with live `ADI_BOOT_DATA`, consume its source
mutation, invoke its callback after loading MAIN and jump via EVT1. No host code
imitates those INIT/callback operations. The loader and bcore dependency pins are
unchanged. The entire source, linker configuration, rebuild command and identity
manifest are in [the fixture](../tests/fixtures/init_handoff/README.md).

A real `MT29F4G08` snapshot backend with an explicit generated arithmetic pattern
is attached to both paths. The worker never replaces it with `NoFlash`. Memory
boot does not consume NAND: zero NAND commands are required by the regression.
This deliberately qualifies the shared memory-boot protocol, not NAND boot,
NAND-context mutations, ECC or physical block selection. No private input is used.
Each worker has an isolated directory so an unrelated private `otp.bin` cannot be
picked up. The unchanged OTP constructor may create its model default there;
this output is neither an owner OTP input nor an uploaded artifact. Preboot is
not entered.

## Exact execution bounds

Use `tools/run_init_handoff.py`, which supervises the native worker
`op1-init-handoff`. Native invocation alone is not the hard wall-time contract.
The supervisor covers worker startup and compilation as well as execution.

| Bound | Enforcement |
|---|---|
| Guest cycles <= 1,000,000,000 | Opt-in core limited mode selected before compilation; at most one issue packet per Run. IDLE is clamped before advancing guest time. |
| CPU Run calls <= 5,000,000 | Count every invocation before calling; no extra Run for inspection or shutdown. |
| Worker wall time <= 120 seconds | Independent parent deadline kills a blocked worker, including compiler threads. OS scheduling determines kill latency; cleanup/reporting can follow termination. |
| Retained events <= 4,096 per source | Explicit cap for each startup category, the dedicated Port G history, unique-PC/access tracking and handoff snapshots; omitted counts remain visible. |

Lower positive limits are accepted. Larger, zero, negative or malformed limits
are rejected. Contract/summary metadata and aggregate counters are not trace
events. After the unique-PC cap, omissions count untracked visits, not necessarily
distinct PCs. First/change read coalescing is separately described by the trace
contract; it is not a complete MMIO-read log. Existing trace budgets are retained
when callers omit the new optional cap. Existing frontend/debugger block-based
limits are not promoted to this stricter contract.

`completed`, `guest_rejected`, `guest_failure`, `contract_failure`, `cycle_limit`,
`run_limit`, `host_timeout`, `worker_failure` and `incomplete_observation` remain
distinct. A completion signature alone cannot qualify a claimed ROM path without
observed ROM, INIT, callback and MAIN entries. A capped trace cannot report a
successful complete observation. A killed worker has no authoritative final CPU
snapshot or dropped counts: its result explicitly marks both unavailable. Never
substitute an older result or a partial log for that missing observation.

Snapshots at supported boundaries include registers, return state, live boot
fields where safely addressable, CEC/SIC, raw PLL/control storage, memory controls,
mailbox/context and the SDRAM sentinel. Reads do not execute another guest packet.
The unit regression separately verifies one-packet execution, an exact 257-cycle
loop ceiling, a seven-cycle IDLE ceiling and a two-event history/PC cap without
changing raw Port G storage.

## Reproduce

Use the existing CMake dependency cache/toolchain and normal test configuration.
No new dependency is introduced.

```sh
cmake --build build --target op1-init-handoff op1-bounded-execution-test
python3 tests/fixtures/init_handoff/build.py \
  --as "$BFIN_AS" --ld "$BFIN_LD" --output build/init-rebuild --check
python3 tools/run_init_handoff.py \
  --binary build/op1-init-handoff \
  --image tests/fixtures/init_handoff/normal.ldr \
  --nand build/init-rebuild/synthetic.nand \
  --output build/init-example \
  --max-cycles 1000000000 --max-runs 5000000 --wall-ms 120000 --trace-cap 4096
ctest --test-dir build --output-on-failure \
  -R 'op1-(bounded-execution-test|init-handoff)$'
```

Use a fresh output prefix: the supervisor refuses existing outputs or input
collisions. It also rejects an image or NAND backing whose identity differs from
the synthetic manifest, before starting a worker. `--path direct` with `direct.ldr` is the ordinary MAIN control;
`--path direct-stream` with `stream.ldr` demonstrates failure of direct INIT
invocation. NAND/reset boot and other paths are explicitly unsupported.

The Python integration test generates its own backing pattern, checks all input
hashes, runs both positive repeats and negative controls, and writes
`build/init-handoff/validation.json`. The per-run supervision receipt identifies
the native executable, image, manifest, backing and public UI by SHA256. CI checks
fixture reconstruction and runs the native regressions in all existing build
configurations. Only the generated JSON/JSONL reports are added to artifact
uploads, not NAND, OTP, ELF or private evidence.

## Reproduced state transitions

For the committed normal image, the local deterministic run retires 2,261
packets in 2,261 Run calls, including 2,069 calls in ROM, and reaches completion
at 90,156 model cycles (the rest are IDLE cycles). These are functional issue
counts at the existing fixed 400-MHz time model, not measured hardware timing.

| Observation | ROM-mediated path | Direct identical MAIN section |
|---|---|---|
| INIT | Entry with live scratchpad pointer `FFB00F84`; one validated call and return to ROM | Not executed |
| Loader source | INIT adds exactly 16 bytes, skipping the poison header | No ROM boot data |
| Callback | Same live R0; scratchpad buffer in R1; single-block flags 13 in R2; one validated invocation | Not executed |
| Loaded test word | `A1B2C3D4` -> `55AA11EE` in guest callback | `A1B2C3D4` |
| Bank/refresh | Guest requests SDBCTL=1 / SDRRC=0300 with 16-bit stores | Constructor 0 / 081A |
| Memory | Guest dirties a mailbox word to FFFFFFFF; ROM FILL clears it to zero; guest writes `13579BDF` sentinel; MAIN copied after INIT | No INIT sentinel |
| SIC wake masks | Guest requests IWR0=1, IWR1=0 | Constructor FFFFFFFF / FFFFFFFF |
| SYSCFG | Guest changes 30 -> 32 | 30 |
| Entry | ROM FIRST sets EVT1=00100000; actual MAIN entry observed | Loader sets PC directly; EVT1 remains 0 |
| Stack | Synthetic guest caller selects scratchpad; ROM restores it at transfer | Constructor SDRAM stack |
| PLL storage | Unchanged raw constructor values | Same raw values |

The source/callback, memory sentinel, wake masks and SYSCFG changes are observed
across INIT entry/return and callback boundaries. Stack selection and ROM EVT1
handling belong to the synthetic caller/ROM transfer, not to INIT alone. This
separation prevents attributing every direct-loader difference to initialization.

Both native state receipts and the selected event/history files are byte-identical
across the positive repeats. The integration summary records their SHA256 values.
The normal image SHA256 is
`0edac2d7b2188bb3c3a55d5e3e9adc8c3a4156dcbf7a23577f68a7c09945610f`;
the MAIN payload, shared by every case, is
`f5715114ac2d4276b9d12104000e5eeba0424768eed941b0e5c94e9f237922fd`.
The generated NAND-pattern hash is
`6c0294b06b53f1e89f9978a127bf9ee4182239125570f700aa47f39d79a6d278`.

Negative controls publish E001 for null/misaligned/malformed data, E002 for bad
callback context, E003 for omitted synthetic memory setup, and E004 for missing
INIT or ignored source redirection. E007 identifies a missing zero fill: the
bootstrap first writes a dirty marker, so constructor zeros cannot satisfy this check. Directly running the stream's first DXE
produces E001. Requesting a ROM qualification for the direct image produces
`contract_failure`. Reduced cycle/Run limits stop exactly at 19 cycles / 7 calls;
a one-event cap reports incomplete observation; a one-ms parent deadline reports
a killed worker and unavailable final state. Above-ceiling options and NAND boot
are rejected before guest execution.

## Remaining qualification

SDRAM is still backed independently of EBIU enable/status in this emulator. The
bad-init control checks the requested control state and sentinel; it is not a
physical bus-fault test. Cache/CPLB operation, barriers, PLL lock and CLKIN,
OTP/PBS/reset policy, NAND boot and ECC, board routing and the owner's installed
MAIN/ROM identity remain unqualified. No codec setup, forced SPORT clock,
serialized PCM, analog output or physical device success is claimed. R4 and its
raw hysteresis refusal remain preserved. This milestone starts no hardware work
or subsequent implementation phase.

## Local validation receipt

The local macOS build used Apple clang 17.0.0 and LLVM 15.0.7. The focused
native regressions pass 2/2, including 17 integration runs/controls, and the
GNU 2.44 fixture rebuild matches every committed image and source/symbol manifest.
The full native suite passed 40/41: the unchanged USB/IP loopback test failed
its connection check, and a separate loopback-listen check returned EPERM in
this execution environment. No permission change or test relaxation was made.
The 15 provisioning/tool unittest cases pass. The local system Python lacks
pytest; it was not installed. The existing CI Python job and all three native
configurations remain enabled to provide their independent results.
