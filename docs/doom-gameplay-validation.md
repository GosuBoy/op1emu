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
The implementation commit and cross-platform CI receipts are recorded below
when the branch validation completes.
