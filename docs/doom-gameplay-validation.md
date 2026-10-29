# Gameplay scene validation

Local validation on Apple silicon passes all 36 CTests. Stock GNU Binutils
2.44 rebuilds all ten public images and verifies their source/symbol manifest.
The seven earlier stream images and both finite images are unchanged.

The new work-44100 case validates ISR submission of READY buffers during
instruction work, complete PCM/cadence/ownership checks, register preservation
and repeat/chaining equality. Public CI runs this through the existing
op1-stream-guest-44100 test in Linux profiling OFF/ON and macOS configurations.
CI does not contain or execute the private game scene.

The separately retained private qualification completes normal, sound-disabled
and overload cases, twice unchained and once chained. Each case has identical
PCM/CSV/JSON/selected pixels across those three runs. Normal completes 350 game
tics and frames, nine real pistol actions, one barrel explosion, 48 mixer
commands, 882,048 fresh words and 6,891 DMA interrupts. All samples match both
actual-mixer host chunk sizes and an independent closed-form arithmetic oracle.
There are 974 saved interrupt PCs inside renderer functions. Silent game states
and selected pixels match normal; its PCM is zero. Overload preserves 441,216
fresh words, 194 complete replays and 195 underflow observations before bounded
stop, with no future fresh output. All ten storage guards survive.

A relocated source rebuild produces identical private images. The preserved
standalone mixer/pause/loss/delay matrix also passes, with every prior output
identity unchanged. Exact private source/toolchain/input/output identities,
reproduction commands, raw traces and selected frame pixels stay in the
private report/checkpoint, outside public commits and CI artifacts.

See [execution contract and limitations](doom-gameplay-scene.md).

## Implementation and CI receipt

Implementation [5e371584aa85e344924a6cc42a87409c6813db19](https://github.com/GosuBoy/op1emu/commit/5e371584aa85e344924a6cc42a87409c6813db19).
[CI run 40904405319](https://github.com/GosuBoy/op1emu/actions/runs/40904405319)
completed successfully in all six jobs:

- Linux profiling OFF: 36/36 CTests.
- Linux profiling ON: 39/39 CTests.
- macOS Apple silicon: 36/36 CTests.
- Stock Blackfin Binutils: all ten images, source hashes and symbols verified.
- Python: 15 provisioning tests and 183 tool tests.
- libc++: TimeSource contract passes.

All 224 public PCM/CSV/JSON data and identity files (142 from the streaming
fixtures) match local Apple silicon output byte for byte in all three native
CI artifacts. All 154 prior PCM/CSV files match the preceding implementation's
40879883311 CI artifact unchanged. Console logs are excluded from identity
comparisons. Artifact contents contain only the public sport/codec/stream
fixtures; no private engine, assets, frame pixels or scene captures are present.

| Artifact | ID | Downloaded archive SHA256 |
|---|---:|---|
| Linux OFF |12911550726|`f9e9ad0f0ff7873ca04a8ce99812b9a3548f25625eef5eabc41b3158c0e4fe8f`|
| Linux ON |12911315968|`91f769db4b26e73f681661b0fa7cd444bb80ab17ba46d15b6c8a57b5684cf761`|
| macOS OFF |12911122162|`f307be63e52de31eb34b8e898bf5a217bc2c35d6ad6497e581f7ec0523c8033a`|
