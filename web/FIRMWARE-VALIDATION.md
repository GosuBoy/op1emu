# Firmware preparation validation

The current preparation flow writes the original stock 246 boot streams and
factory content to NAND, stores the supplied 8 KiB OTP image unchanged, and
starts through the same emulated boot ROM as a manual NAND/OTP import.

- The Node suite covers canonical package gating, NAND geometry, boot ECC and
  padding, byte-preserving OTP storage, atomic selection, cancellation, and
  reuse of the same boot path. The optional independent mkyaffs2 comparison
  checks all data pages and translated OOB bytes for synthetic content trees.
- The source-only Chrome harness checks the setup disclosure and runs three
  real 528 MiB imports into OPFS. After each import it checks that only the
  current NAND remains. With a local stock update it also checks preparation,
  the resulting 11,810 content pages, reload, OTP export, and preservation of
  the active pair after failed or cancelled preparation. This uses synthetic
  OTP bytes and a stub manifest to isolate preparation from WASM execution.
- The same JavaScript writer produced a 528 MiB image from the stock package
  for a local native emulator test. Both loader ranges compared byte-for-byte
  with the package members. Native tracing confirmed ROM reads from block 0
  and the handoff to the encrypted stream at block 4, including OTP access.
  The test also captured a TE-BOOT startup frame.
  A matching device OTP is still needed to verify a complete boot to the
  instrument screen and audio. Mobile and low-memory behavior remain untested.

The BF52x ECC layout follows the
[Analog Devices NAND controller reference](https://www.analog.com/media/en/technical-documentation/application-notes/ee-344_rev1.pdf)
and [ADI's BF5xx driver's ROM-compatible spare layout](https://android.googlesource.com/kernel/common/+/a1ec56ed9f42fb5374cd2add373811c9527c0995/drivers/mtd/nand/bf5xx_nand.c#116).

From `web/`:

```sh
npm ci
npm test
MKYAFFS2=/path/to/mkyaffs2 npm test
node tools/firmware-browser.mjs
OP1_FIRMWARE=/local/path/to/op1_246.op1 node tools/firmware-browser.mjs
```

The optional stock update is a local input. CI uses synthetic data.
