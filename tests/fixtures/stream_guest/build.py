#!/usr/bin/env python3
"""Build the public streaming assembly fixture with GNU Binutils 2.44 (bfin-elf).

No C compiler, vendor headers, ELF tools or third-party Python modules needed.
--check verifies a rebuild against the committed image and symbol manifest.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def extract(data):
    assert data[:7] == b"\x7fELF\x01\x01\x01", "expected little-endian ELF32"
    assert struct.unpack_from("<H", data, 18)[0] == 106, "expected EM_BLACKFIN"
    entry, _, shoff = struct.unpack_from("<III", data, 24)
    shsize, count, names = struct.unpack_from("<HHH", data, 46)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shsize) for i in range(count)]
    def contents(s):
        return data[s[4]:s[4] + s[5]]
    def string(table, offset):
        return table[offset:table.index(b"\0", offset)].decode()
    strings = contents(sections[names])
    text = [s for s in sections if string(strings, s[0]) == ".text"]
    assert len(text) == 1 and text[0][3] == entry == 0xffa00100
    assert all(s == text[0] or not (s[2] & 2) or s[5] == 0 for s in sections), "unexpected allocated section"
    symbols = {}
    for s in sections:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strings = contents(sections[s[6]])
        for offset in range(s[4], s[4] + s[5], s[9]):
            name, value, _, info, _, index = struct.unpack_from("<IIIBBH", data, offset)
            if info >> 4 == 1 and index:  # defined global symbols
                symbols[string(strings, name)] = value
    payload = contents(text[0])
    header = bytearray(struct.pack("<IIII", 0xad00c000, entry, len(payload), 0))
    checksum = 0
    for byte in header:
        checksum ^= byte
    header[2] = checksum  # XOR of complete 16-byte header is zero
    return bytes(header) + payload, symbols


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--as", dest="assembler", default="bfin-elf-as")
    p.add_argument("--ld", dest="linker", default="bfin-elf-ld")
    p.add_argument("--check", action="store_true")
    args = p.parse_args()
    for tool in (args.assembler, args.linker):
        version = subprocess.check_output([tool, "--version"], text=True).splitlines()[0]
        assert "2.44" in version, version
        print(version)
    images = {}
    for name, rate, case in [(n, 48000, i) for i, n in enumerate(("normal", "delay", "premature"))] + [
            (n + "-44100", 44100, i) for i, n in enumerate(("normal", "delay", "premature", "stop-delay"))]:
        with tempfile.TemporaryDirectory(prefix="stream-guest-build-") as tmp:
            obj, elf = Path(tmp) / "guest.o", Path(tmp) / "guest.elf"
            subprocess.run([args.assembler, "-mcpu=bf524", "--fatal-warnings", "-I", ROOT,
                            "--defsym", f"STREAM_CASE={case}", "--defsym", f"STREAM_RATE={rate}", "-o", obj, ROOT / "stream.s"], check=True)
            subprocess.run([args.linker, "-T", ROOT / "link.ld", "-o", elf, obj], check=True)
            image, symbols = extract(elf.read_bytes())
        path = ROOT / (name + ".ldr")
        images[name] = {"case": case, "rate_hz": rate, "image_bytes": len(image), "image_sha256": sha(image), "symbols": symbols}
        if args.check:
            assert image == path.read_bytes(), f"{name} LDR rebuild differs"
        else:
            path.write_bytes(image)
        print(f"{name}: {len(image)} bytes SHA256 {sha(image)}")
    manifest = {
        "schema": 1,
        "toolchain": "GNU Binutils 2.44, bfin-elf; assembler -mcpu=bf524; no C compiler/runtime",
        "sources": {name: sha((ROOT / name).read_bytes()) for name in
                    ("stream.s", "codec.inc", "codec-macros.inc", "link.ld", "build.py")},
        "images": images,
    }
    if args.check:
        assert manifest == json.loads((ROOT / "manifest.json").read_text()), "manifest differs"
    else:
        (ROOT / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
