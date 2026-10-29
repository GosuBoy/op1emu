# Bounded firmware development readiness

This closes the existing no-flash development baseline. The workflow below
reuses the streaming fixture builder, independent capture oracle, native
observer and `op1-debug`. It adds no CPU/device/debugger behavior.

| Claim | Evidence and limit |
|---|---|
| Adapted RAM gameplay and audio | The existing private scene source rebuild reproduces its unchanged image; 350 game tics/frames and all preserved logical outputs match. This is the adapted RAM environment. |
| Deterministic read-only debugging | Public source diagnosis and private ISR/renderer/submission stops reach exact supported boundaries. Inspection and host pauses preserve complete guest outputs. Arbitrary instruction addresses may remain unreached. |
| Hardened debugger cross-platform tests | The one additional targeted macOS retry of test revision `373096e` ran and passed 38/38; Linux OFF 38/38 and ON 41/41 were already passing. New workflow revision coverage is recorded separately below. |
| Unchanged V74/R4 or physical equivalence | Unqualified. Raw Port G hysteresis `0x0008`, R4 admission refusal, synthetic panel status, legacy default and hardware/analog gaps are preserved. No device access or flashing occurred. |

## One local entry point

Use an existing CMake build for this checkout. Dependencies must already be
provisioned. For native building select `FETCHCONTENT_FULLY_DISCONNECTED=ON`
in that configuration; the wrapper also forces Cargo offline. It performs no
downloads or automatic compiler/input substitution. Configure/build prerequisites
remain the repository's existing CMake instructions.

```sh
python3 tools/firmware_workflow.py public --build-dir build --build-native \
  --rebuild --assembler /absolute/binutils-build/gas/as-new \
  --linker /absolute/binutils-build/ld/ld-new \
  --output /tmp/op1-public-readiness
```

The output must be explicit, absolute and new/empty. The native build directory
must belong to this checkout. The workflow verifies source pins, records exact
compiler/assembler/linker and native executable identities, builds the three
guest images, executes bounded captures, checks the existing oracle, diagnoses
B through the actual `op1-debug` command interface, resumes and compares.
The complete commands, matching symbols, source patches, logs and identities
are retained. `readiness.json` records the outcome.

`--prebuilt` explicitly selects the pinned committed images instead of
`--rebuild`. It verifies sources/images and performs the same run/debug/compare
checks, but its receipt states **source rebuild not claimed**. Native CI uses
this mode; the separate Blackfin job independently rebuilds and verifies all
three images. No private dependency is required by either job.

Missing tools/inputs and identity mismatches fail. Unexpected negative-test
failures also fail. Existing evidence is never overwritten. A subprocess host
timeout returns 124 with `host-timeout`; this is separate from the guest's
bounded watchdog and the intentionally expected sample-oracle failure.
`op1-debug` also retains its existing cycle, Run-call and host-time limits.

## Source defect, diagnosis and correction

The [fixture](../tests/fixtures/diagnose_guest/README.md) derives three actual
assembly sources and separately assembles/links A, B and C with GNU Binutils
2.44, `bfin-elf`, BF524. No historical object files are reused. A is the
reference `work-44100` producer; B changes only the right-channel tag literal
from `0x40` to `0x20` in `fill_words`; C corrects that source literal and rebuilds.

The unmodified independent oracle rejects B with
`generation/index mismatch at fresh component 1`. The guest itself completes
normally: this is a sample defect, not a guest watchdog or host timeout.
The matching B symbols place `start_dma` at `0xffa00512`; that exact supported
boundary was reached and verified. Across a minimum 50 ms host pause, passive
RAM inspection reads:

```
0x00800000  0x00a00000  0x00810000  0x00a10000
```

The expected first right word is `0x00c00000`. Both ownership words are READY
(2), DMA work is zero and DMA is stopped. The error already exists in the
producer's RAM before first DMA submission. The source patch explains it.
Removing the stop and continuing preserves B's complete defective PCM/CSV;
inspection does not repair the guest. C passes the full original oracle and
matches A's complete PCM, timestamp CSV and event JSON byte for byte. No
debugger writes, oracle relaxation, capture edits or timestamp normalization
are used. Each capture contains 32,768 serial words from 256 64-frame blocks.

| Identity | SHA256 |
|---|---|
| A/C LDR | `98528c35d07b6f584c1eb9aa0edabb8c5deb696c4ba7d3374ed898abceec765d` |
| B LDR | `aa3922c3031c091388bd69f9e7dc2c3a7d97916a092207aded241d51015a9ab3` |
| A/C PCM | `fc7e7b88a9eac4c0afa054a9c3b33a67d0fc9d58563ef35c985cffe42a7983b2` |
| B PCM | `5634ac4cbb859dfddfd08e212cf850522dc7e448386b0133dfe22d6896f0dd01` |
| A/C timestamp CSV | `91858c0d53aa2fc64258742796639a3e9fbbe4e53c0bf7376418dede81541c6a` |
| A/C event JSON | `dd086ae79a19fbb2bb30c21eb98a89b9010dec6e8b405bd347a39ca4b0e71a48` |

Local assembler SHA256 is
`2a681a9686e85fe5c77cbfd5af31e8705211b9fb6e239893d5833b021cbc1e98`;
linker is `073b956c1980058ccf1be6ea90b18bb58588a875189f6ea6be6aadaadfaa00d4`.
Tool binary identities are platform-specific; the independent GNU-source CI
build must reproduce the same images, symbols and source manifest.

## Private profile

```sh
python3 tools/firmware_workflow.py private --build-dir build --rebuild \
  --recipe /absolute/private/recipe.json \
  --output /absolute/private/new-readiness-output
```

The recipe and output must be outside the public repository, including its
build directories. The recipe schema is 1, with explicit absolute `runner`,
`scene`, `gcc`, `binutils` paths and `pins` mapping every required file path to
SHA256. The private runner itself, source/glue/linker files, assets, compiler,
cc1, assembler/linker, image/map/build receipt, comparison helpers and preserved
reference outputs are pinned. Rebuild tool paths must resolve to pinned files.
The supplied runner accepts `--repo`, `--scene`, `--output`, `--build-dir`, and
for source rebuilding `--rebuild --gcc --binutils`. It must emit a passing
`private-result.json`; the wrapper never downloads or manufactures private
inputs. This fixed adapter lives with the existing private scene, not in CI.

The closeout verified 256 input identities, copied source into a fresh private
output, rebuilt the entire existing scene consistently and reproduced its
unchanged image. The existing comparison helper then ran uninterrupted,
debug-enabled and paused/inspected/resumed cases against the original reference.
All eight files per case match: complete PCM/CSV/JSON and five selected frames.
The same ISR, renderer and submission stops are verified at exact supported
boundaries with repeated passive inspection across 50 ms host pauses. A further
terminal `op1-debug` renderer stop/resume also preserves complete PCM/CSV.

All 350 tics/frames, 48 sound commands, 882,048 native words, 6,891 DMA interrupts,
974 renderer interrupts, guards, ownership and pixel comparisons are retained.
Private sources, detailed provenance, addresses, RAM, logs, maps, assets and
captures remain outside public commits and CI artifacts. Existing scene and
debugger receipts/archives are immutable; the new private receipt records its
explicit paths and reproduction commands. Public CI checks rejection using
synthetic missing/mismatched inputs only.

## Verification receipt

Local Apple-silicon source-rebuild workflow and all **39/39 CTests** pass.
The previous debugger implementation is unchanged. All 247 prior public logical
outputs (245 PCM/CSV/JSON plus two debugger logs) match the now-executed macOS
hardened-test artifact and both Linux configurations byte for byte.

[Historical CI 40916758992](https://github.com/GosuBoy/op1emu/actions/runs/40916758992)
is successful at exact test revision
`373096e950c475c1a7872fd42e8d6091abb2c8ac`. Attempts 1 and 2 had unexecuted
macOS cancellations; attempt 3's [macOS job](https://github.com/GosuBoy/op1emu/actions/runs/40916758992/job/123626976130)
executed setup/build/test/upload and passed 38/38. Exactly one targeted retry
was requested in this closeout. Its artifact is `12920716563`, SHA256
`b2c37edaa773371c3ff56579369f394331f5cff0a21860f98dc54f4a74e59de1`.
This closes the previously recorded hosted-Mac gap for that exact revision;
it does not change the historical cancelled attempts or establish their cause.

New workflow revision CI and artifact comparison results will be recorded here
separately after that revision executes. Public capture uploads include only
synthetic sport/codec/stream/debugger/readiness fixtures.
