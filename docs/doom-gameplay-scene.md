# Bounded gameplay, renderer and audio scene

The private source-built experiment now runs 350 real game tics with the
renderer active while game-generated sound requests feed the recovered mixer
and qualified 44.1 kHz streaming path. Source, engine adaptations, WAD,
provenance, images, selected frames and raw captures remain outside public Git
and CI. Public CI tests the reusable guest consumer and observer support.

This is a separately linked RAM guest, not unchanged V74/R4 startup. The
legacy default, R4 admission refusal and raw hysteresis storage are unchanged.
No physical device, analog output, board routing or hardware deadline margin
is qualified. Music, level transitions, RTOS integration, hardware descriptors
and recovery after loss of stereo alignment remain separate work.

## Execution and causal evidence

The recovered engine defines `TICRATE=35`: 350 completed tics represent ten
seconds. After startup/wipe and two warmup game tics, scripted key transitions
enter through the platform key callback, engine event queue and normal game
responder. They cause movement, turning, use and firing; there is no scheduled
list of sound commands and no direct mutation of player/world state.

Nine real pistol actions consume nine rounds (50 to 41) and issue nine real
sound requests. A barrel explosion supplies the tenth request and a second
overlapping voice. Its later explosion action reduces player health from 100
to 74. Read-only hooks record input acceptance, actions, ammo, origins,
game-state progression, sound requests and the eventual mixer commands.
The retained run has ten starts, eight stops, thirty parameter updates, six
replacements before the previous voice's natural end and two overlapping
starts. These calls preserve the parameters requested by this scene; they
do not independently qualify all spatialization cases.

The actual 440 by 200 renderer completes 350 frames. A guest publication
contains the framebuffer pointer, dimensions, game tic and 64-bit guest cycle
stamp, followed by SSYNC and IDLE. The optional native observer reads it at
IDLE checkpoints. It records every publication and saves actual pixels at
tics 1, 35, 105, 175 and 350. It does not synthesize frames or advance time.
These are engine framebuffers, not an OP-1 panel/display qualification.

## Sound data, timing and ownership

All linked engine, runtime, adapter and assembly components are rebuilt from
source with the identified compatible GCC 13.2.0/Binutils 2.44 toolchain. No
historical object is reused. Source/header/input hashes, exact commands,
adaptation diffs, linker reservations, ELF functions and image hashes are
retained privately. Rebuilding in a different directory produces identical
images using an explicit compiler file-prefix mapping.

The real sound module resolves the engine's names/aliases into an immutable
WAD provider, validates DMX spans and uses the recovered decoder/mixer.
Generation-tagged handles support playing queries, parameter updates, stops
and channel replacement. Missing, malformed, short, out-of-bounds, invalidated
and alias-cycle inputs have explicit private controls. Shutdown prevents new
requests but retains sample memory until transport drain; invalidation follows.

Requested sample time is the scene tic times 1260 at 44,100 Hz. The adapter
applies a late event at the first unrendered sample, records requested/applied
positions and refuses lateness beyond 128 frames. It never rewrites READY or
IN_DMA data. Observed maximum lateness is 128 frames (2.902494 ms); the queue
peaks at three entries within its 128-entry bound. Recorded applied events
split rendering inside 64-frame calls. The existing mixer retains voice and
source-rate state between calls; no output resampling is added.

Substantial mixing stays in main context, with cooperative service points
inside real game/raster/copy work. No render work is skipped. Two 64-frame
slots retain FREE/FILLING/READY/IN_DMA ownership, SSYNC publication and finite
32-bit DMA4 SYNC1 transfers. Guest TWI establishes the existing codec profile
from synthetic MCLK/reset/wiring; no direct SPORT clock is supplied.

The scene exposed a guest driver deadline: after SYNC1, six buffered words
provide only 68.027 microseconds of serial tail. Main-context restart could
miss that interval even when the next slot was READY. Optional
`STREAM_ISR_RESTART` now lets the acknowledging ISR submit only that READY
successor. Main context still releases completed storage and produces data.
The ISR preserves the additional P2 scratch register; private C integration
also preserves/reloads P5 and respects argument home space. Missing READY is
recorded, and the diagnostic never resumes fresh audio after detected TUVF. The normal IRQ-to-submit ledger interval is 136 guest
cycles (0.34 microseconds), a functional emulator observation.

The public `work-44100.ldr` regression alternates 1.7 ms and 0.2 ms of real
guest instruction work after publication. It requires interrupts during that
work, a surviving P2 sentinel, all sample/slot/cadence/ownership checks and
repeat/chaining equality. Existing seven stream images and both finite
diagnostic images remain byte-identical. This corrects the selected guest
driver, without changing the emulator peripheral model or claiming hardware
bus timing.

## Results and reproduction

Normal captures 441,024 stereo frames (882,048 words): ten seconds plus 24
zero tail frames. Every sample/slot/replay flag and rational timestamp is
checked. The complete capture equals offline replay of the recorded applied
events through the recovered mixer using both 64- and 512-frame chunking.
An additional closed-form arithmetic oracle evaluates source indices, DMX
bytes, integer gains, signed truncation and saturation independently, and
also matches the complete capture. The preceding standalone generated-vector
checks remain intact.

There are 6,891 DMA completions/guest interrupts and 3,446/3,445 productions
per slot. Source-read and peripheral-acceptance digests match independently;
each completion precedes its last six serialized words. Normal has no data
replay or overflow, with the existing single accounted post-data underflow
during final shutdown. Saved return PCs include 974 interrupts inside renderer
functions, alongside C game/runtime/palette work. Storage guards remain intact;
observed stack high-water is 1,468 bytes and peak heap use is 6,679,128 bytes.

The sound-disabled run completes the same scene with zero PCM; all 350
recorded world states and five selected pixel captures match normal. A
deliberate 4 ms producer stall during rendering stops after 175 frames with
`UNDR`, preserving completed replay words and the partial-underflow count;
fresh data never resumes. Each case passes two unchained runs and one chained
run with byte-identical PCM, CSV, JSON and selected pixels. The preserved
standalone mixer, pause, permanent-clock-loss and delay-stop matrix also
passes with unchanged output identities.

The private runner bounds execution by guest cycles, Run calls and wall time.
Failures retain PC, interrupt state, DMA address/count/status, SPORT buffers,
ownership, ledgers, guards, stack observation and serialized word count.
All trace truncation is counted and fails validation.

```sh
# Public support, without private inputs:
cmake --build build -j6
ctest --test-dir build --output-on-failure
python3 tests/fixtures/stream_guest/build.py --as bfin-elf-as --ld bfin-elf-ld --check

# PRIVATE_SCENE is the separately retained private source/checkpoint directory.
python3 "$PRIVATE_SCENE/build_scene.py" --repo "$PWD" \
  --gcc "$BFIN_GCC" --binutils "$BFIN_BINUTILS_BUILD"
python3 "$PRIVATE_SCENE/run_scene.py" --repo "$PWD"
```

The private report supplies exact source/toolchain/asset/output identities and
launch commands. See the commit-bound [validation receipt](doom-gameplay-validation.md).
