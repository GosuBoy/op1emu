# Public source diagnosis exercise

This uses the existing 44.1 kHz `work` streaming assembly guest, codec setup,
linker reservation and normal LDR execution path. GNU Binutils 2.44 for
`bfin-elf`, `-mcpu=bf524`, builds all linked code; no C compiler or runtime is
needed. No private firmware, sounds or game sources are required.

`build.py` derives and builds three separate sources:

- A/reference: unchanged `stream_guest/stream.s`.
- B/defective: only `fill_words`' right-channel tag changes from
  `R2 = 0x40;` to `R2 = 0x20;`.
- C/corrected: correct that literal in B and assemble/link again.

A/C are byte-identical source and images. B is a real producer-source defect,
not a debugger write, capture edit or weakened oracle. Its symbol addresses
can differ from A: use each image's own matching manifest.

```sh
python3 tests/fixtures/diagnose_guest/build.py \
  --as /absolute/binutils-build/gas/as-new \
  --ld /absolute/binutils-build/ld/ld-new \
  --output /tmp/diagnose-build --check
```

The builder records tool binary hashes/version, source hashes, exact commands,
image hashes and symbols. Its assembler/linker source/build recipe is the same
pinned GNU recipe as [the streaming fixture](../stream_guest/README.md).
CI independently rebuilds all three images and checks the complete manifest.
`manifest.json` pins the committed images and derivation inputs.

The single [developer workflow](../../../docs/firmware-readiness.md) builds,
runs, diagnoses and compares them. The expected negative is an independent
sample-oracle failure after normal guest completion. The diagnostic stop is
before first DMA submission, with both producer buffers READY, so the wrong
word already exists in guest-produced RAM. Source correction must reproduce
complete A PCM/CSV/JSON; resumed B must preserve B's defective PCM/CSV.
