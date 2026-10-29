# Provision an OP-1 NAND image (experimental)

`op1emu` emulates NAND flash; it does **not** automatically install the factory
files contained in an `.op1` update archive. This tool prepares a **new** NAND
image containing the original firmware's `content/` directory, laid out as a
YAFFS2 filesystem.

The result uses op1emu's MT29F4G08 format: 262,144 × 2048-byte data pages,
followed by 262,144 × 64-byte out-of-band (OOB) records (528 MiB total).
It does **not** modify the emulator's CPU, NAND controller, or YAFFS code.

## Requirements

- Python 3 with standard-library `lzma` support
- A working `mkyaffs2` executable from [yaffs2utils](https://github.com/ljvblfz/yaffs2utils)
- Your own original OP-1 firmware package or an already-unpacked `content/` directory

For example, on macOS:

```sh
git clone https://github.com/ljvblfz/yaffs2utils.git
make -C yaffs2utils CC=clang mkyaffs2
```

**macOS caveat:** This older version of yaffs2utils has a directory traversal
portability issue: the two `dirname(mkyaffs2_curfile)` calls in `mkyaffs2.c`
need to restore the path by modifying the original buffer rather than
assuming `dirname()` does it. If scanning succeeds but image creation fails
at a nested directory, fix both instances by replacing:

```c
if (!strcmp(dirname(mkyaffs2_curfile), "."))
    mkyaffs2_curfile[0] = '\0';
```

with:

```c
char *slash = strrchr(mkyaffs2_curfile, '/');
if (slash) *slash = '\0';
else mkyaffs2_curfile[0] = '\0';
```

Then rebuild `mkyaffs2`. This workaround was used for the macOS proof of concept;
the upstream utility is not bundled in this repository.

## Usage

From the op1emu repository root, with a local firmware update package:

```sh
python3 tools/provision_nand.py \
  --firmware /path/to/op1_246.op1 \
  --mkyaffs2 ./yaffs2utils/mkyaffs2 \
  --output nand-246.bin
```

Or use an already-extracted `content/` directory:

```sh
python3 tools/provision_nand.py \
  --content-dir /path/to/unpacked/content \
  --mkyaffs2 ./yaffs2utils/mkyaffs2 \
  --output nand-246.bin
```

`--firmware` validates the package's little-endian CRC32, decompresses its
LZMA-alone payload, and extracts only safe regular files/directories under
`content/`. Both modes run `mkyaffs2` with 2048-byte pages and 64-byte OOB.
The converter relocates 28-byte YAFFS2 tags from OOB offset 2 to offset 4
and writes a split data/OOB NAND image. An existing output is **never overwritten**.
If conversion fails, the partially created output is removed.

The default YAFFS starting block is **132**. This was established by an
empirical OP-1 firmware 246 boot test on an Apple Silicon Mac, not by a
complete proof of the firmware's partition-map internals. You can specify
`--start-block N` for independent experiments. Other firmware revisions and
hardware configurations have not been verified.

### Optional OTP-based LDR preparation (stacked draft)

If you want an executable decrypted LDR as well as the NAND content image,
supply an OTP **locally**:

```sh
python3 tools/provision_nand.py \
  --firmware /path/to/op1_246.op1 \
  --otp /path/to/TE-OP-1-BF524-OTP-dump.txt \
  --ldr-output plaintext.ldr \
  --output nand-246.bin \
  --mkyaffs2 ./yaffs2utils/mkyaffs2
```

Alternatively, **explicitly opt in** to an HTTPS download:

```sh
python3 tools/provision_nand.py \
  --firmware /path/to/op1_246.op1 \
  --fetch-otp \
  --ldr-output plaintext.ldr \
  --output nand-246.bin \
  --mkyaffs2 ./yaffs2utils/mkyaffs2
```

`--otp` and `--fetch-otp` are mutually exclusive and require
`--firmware` and `--ldr-output`. Supported local inputs are the textual
OTP page dump, a 16-byte raw 0xd0 page, and op1emu's 8192-byte `otp.bin`.

The opt-in downloader fetches only
[`Tolsi/op1dumps`](https://github.com/Tolsi/op1dumps)'s public text dump
at an **immutable pinned Git commit**, checking the downloaded data against
its pinned Git blob SHA-1. It uses the key locally in memory and does **not**
save an OTP copy, download automatically, put OTP data in Git, or send key
data to a service. Neither key nor firmware contents are printed. No network
request is made unless `--fetch-otp` is provided.

**Scope and caution:** The integrated decryption code implements the
documented XTEA transformation for encrypted OP-1 LDR callback blocks.
Its offline tests use synthetic firmware only; a full `op1_246.op1`
end-to-end comparison with a known-good decrypted LDR is **still required**.
The fixed public dump might not match every firmware or device.
This mode produces a plaintext LDR for op1emu direct-LDR loading, **not**
a boot-ROM-installable encrypted NAND. Downloading from a third-party public
repository is not the same as distributing the key ourselves, but the maintainer
must approve the explicit-fetch policy before upstream acceptance.

### Running

The NAND image contains the factory assets **only**. If OTP decryption was
selected, the utility additionally writes a locally decrypted `plaintext.ldr`.
Otherwise you still need a compatible decrypted LDR or normal boot setup.

```sh
./build/op1emu nand-246.bin /path/to/plaintext.ldr --nand-snapshot
```

Use `--nand-snapshot` when testing; keep a golden image, then copy it if you
want to persist guest writes using `--nand-rw`.

Do not put NAND images, decrypted firmware, OTP dumps, or factory sound
archives in a pull request. These are user-provided inputs, never bundled.

## Tests and validation

```sh
python3 -m unittest discover -s tools -p 'test_*.py' -v
```

The included unit tests use only synthetic page data and a synthetic
CRC32/LZMA/TAR package. They cover the split-OOB layout, overwrite protection, cleanup, package
extraction, CRC rejection, OTP formats, mock offline fetching, download integrity,
and synthetic LDR decryption.

### Status / known gaps

- The manual proof of concept populated 11,810 YAFFS2 pages from 116 factory
  objects, and firmware 246 subsequently reached the synth UI and produced audio.
- The *new combined CLI* still needs end-to-end validation with a real, locally
  owned `.op1` package and upstream review. This is intentionally a **draft**.
- `mkyaffs2` is an external dependency and requires a macOS portability fix
  for the tested version; removing that friction is a possible follow-up.
- This does not create a self-booting encrypted firmware NAND and does not
  replace the normal TE-BOOT installation mechanism.
