"""Opt-in OP-1 OTP loading and original Blackfin LDR decryption.

No OTP material is bundled. The public source is pinned to a Git commit/blob
and downloaded only when the caller explicitly requests it.
"""

import hashlib
from pathlib import Path
import re
import struct
from urllib.request import Request, urlopen


OTP_SOURCE_COMMIT = "e42843c4a31ae88dbe5d7ee2413b02bd5caeff92"
OTP_SOURCE_BLOB = "82e4de41f1c8e773dff9dbdd81b0668940d2081c"
OTP_SOURCE_URL = (
    "https://raw.githubusercontent.com/Tolsi/op1dumps/"
    + OTP_SOURCE_COMMIT + "/TE-OP-1-BF524-OTP-dump.txt"
)
OTP_PAGE = 0xD0
OTP_PAGE_SIZE = 16
OTP_IMAGE_SIZE = 0x200 * OTP_PAGE_SIZE
MAX_DOWNLOAD_SIZE = 64 * 1024
WORD_MASK = 0xFFFFFFFF
XTEA_DELTA = 0x9E3779B9


def _otp_page_from_text(text: str) -> bytes:
    """Decode page d0 from Tolsi's two-little-endian-64-bit-word text layout."""
    hits = []
    for line in text.splitlines():
        match = re.fullmatch(
            r"\s*!?0x([0-9a-fA-F]+)\s+([0-9a-fA-F]{16})\s+([0-9a-fA-F]{16})\s*",
            line,
        )
        if match and int(match.group(1), 16) == OTP_PAGE:
            hits.append(
                int(match.group(2), 16).to_bytes(8, "little")
                + int(match.group(3), 16).to_bytes(8, "little")
            )
    if len(hits) != 1:
        raise ValueError("OTP text must contain exactly one complete page 0xd0")
    return hits[0]


def otp_key_from_bytes(raw: bytes) -> bytes:
    """Accept Tolsi text, an 8-KiB op1emu OTP image, or a 16-byte raw page."""
    if len(raw) == OTP_IMAGE_SIZE:
        return raw[OTP_PAGE * OTP_PAGE_SIZE:(OTP_PAGE + 1) * OTP_PAGE_SIZE]
    if len(raw) == OTP_PAGE_SIZE:
        return raw
    try:
        return _otp_page_from_text(raw.decode("utf-8"))
    except UnicodeError as exc:
        raise ValueError("Unsupported OTP format (expected text, 16 B or 8192 B)") from exc


def read_otp(path: Path) -> bytes:
    return otp_key_from_bytes(path.read_bytes())


def fetch_public_otp_key(opener=urlopen) -> bytes:
    """Network access is never implicit; call only for an explicit --fetch-otp."""
    req = Request(
        OTP_SOURCE_URL,
        headers={"User-Agent": "op1emu-nand-provisioner/0.1"},
        method="GET",
    )
    with opener(req, timeout=15) as reply:
        raw = reply.read(MAX_DOWNLOAD_SIZE + 1)
    if len(raw) > MAX_DOWNLOAD_SIZE:
        raise ValueError("Public OTP download exceeds size limit")
    git_object = b"blob " + str(len(raw)).encode("ascii") + b"\0" + raw
    if hashlib.sha1(git_object).hexdigest() != OTP_SOURCE_BLOB:
        raise ValueError("Downloaded OTP does not match pinned Git blob")
    return otp_key_from_bytes(raw)


def _decrypt_xtea_word(block: bytes, key_words: tuple[int, int, int, int]) -> bytes:
    v0, v1 = struct.unpack("<II", block)
    total = (XTEA_DELTA * 32) & WORD_MASK
    for _ in range(32):
        mix = ((((v0 << 4) & WORD_MASK) ^ (v0 >> 5)) + v0) & WORD_MASK
        v1 = (v1 - (mix ^ ((total + key_words[(total >> 11) & 3]) & WORD_MASK))) & WORD_MASK
        total = (total - XTEA_DELTA) & WORD_MASK
        mix = ((((v1 << 4) & WORD_MASK) ^ (v1 >> 5)) + v1) & WORD_MASK
        v0 = (v0 - (mix ^ ((total + key_words[total & 3]) & WORD_MASK))) & WORD_MASK
    return struct.pack("<II", v0, v1)


def decrypt_ldr(encrypted: bytes, otp_page_d0: bytes) -> bytes:
    """Clear callback/indirect flags and XTEA-decrypt first 8 B of each 24 B.

    Mirrors the original OP-1 LDR block treatment. No key data is printed,
    saved, logged, or included in the repository.
    """
    if len(otp_page_d0) != OTP_PAGE_SIZE:
        raise ValueError("Expected a 16-byte OTP page d0")
    key_words = struct.unpack(">IIII", otp_page_d0)
    output = bytearray()
    offset = 0
    decrypted_blocks = 0
    while offset < len(encrypted):
        if len(encrypted) - offset < 16:
            raise ValueError("Truncated LDR block header")
        head = bytearray(encrypted[offset:offset + 16])
        if head[3] != 0xAD or (0 != _xor_checksum(head)):
            raise ValueError(f"Invalid LDR block header at byte offset {offset}")
        flags = struct.unpack_from("<H", head)[0]
        byte_count = struct.unpack_from("<I", head, 8)[0]
        # FILL payloads are synthesized by the loader; they occupy no file bytes.
        fill = bool(flags & 0x0100)
        offset += 16
        length = 0 if fill else byte_count
        if length > len(encrypted) - offset:
            raise ValueError("Truncated LDR block payload")
        payload = bytearray(encrypted[offset:offset + length])
        offset += length
        if (flags & 0x2400) == 0x2400:
            flags &= ~0x2400
            struct.pack_into("<H", head, 0, flags)
            head[2] ^= 0x24  # flags changed in header's XOR checksum
            for n in range(0, len(payload) - 23, 24):
                payload[n:n + 8] = _decrypt_xtea_word(payload[n:n + 8], key_words)
            decrypted_blocks += 1
        output.extend(head)
        output.extend(payload)
    if not decrypted_blocks:
        raise ValueError("No encrypted LDR callback blocks found")
    return bytes(output)


def _xor_checksum(data: bytes) -> int:
    checksum = 0
    for byte in data:
        checksum ^= byte
    return checksum
