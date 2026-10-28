"""Offline tests for opt-in OTP input handling and OP-1 LDR decryption."""

import hashlib
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent))
import op1_otp


def _encrypt_xtea(plaintext, key):
    """Independent reference encoder used to make a synthetic test fixture."""
    mask = 0xFFFFFFFF
    delta = 0x9E3779B9
    v0, v1 = struct.unpack("<II", plaintext)
    words = struct.unpack(">IIII", key)
    total = 0
    for _ in range(32):
        v0 = (v0 + (((((v1 << 4) & mask) ^ (v1 >> 5)) + v1) & mask ^
                    ((total + words[total & 3]) & mask))) & mask
        total = (total + delta) & mask
        v1 = (v1 + (((((v0 << 4) & mask) ^ (v0 >> 5)) + v0) & mask ^
                    ((total + words[(total >> 11) & 3]) & mask))) & mask
    return struct.pack("<II", v0, v1)


def _ldr_header(flags, byte_count):
    header = bytearray(struct.pack("<HBBIII", flags, 0, 0xAD, 0x1000, byte_count, 0))
    checksum = 0
    for b in header:
        checksum ^= b
    header[2] = checksum
    return bytes(header)


class OtpTests(unittest.TestCase):
    def setUp(self):
        self.key = bytes(range(16))

    def test_otp_text_endianness(self):
        low = int.from_bytes(self.key[:8], "little")
        high = int.from_bytes(self.key[8:], "little")
        text = f"...\n!0xd0  {low:016x} {high:016x}\n0xd1  00\n"
        self.assertEqual(op1_otp.otp_key_from_bytes(text.encode()), self.key)

    def test_otp_bin_formats(self):
        mem = bytearray(8192)
        mem[0xd0 * 16:0xd0 * 16 + 16] = self.key
        self.assertEqual(op1_otp.otp_key_from_bytes(bytes(mem)), self.key)
        self.assertEqual(op1_otp.otp_key_from_bytes(self.key), self.key)

    def test_missing_page_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "page 0xd0"):
            op1_otp.otp_key_from_bytes(b"0xd1  00\n")

    def test_fetched_source_is_pinned_and_opt_in(self):
        dump = "!0xd0  0706050403020100 0f0e0d0c0b0a0908\n".encode()
        gitblob = b"blob " + str(len(dump)).encode() + b"\0" + dump
        blob_sha = hashlib.sha1(gitblob).hexdigest()
        calls = []

        def fake_opener(req, timeout):
            calls.append((req.full_url, timeout))
            return io.BytesIO(dump)

        with patch.object(op1_otp, "OTP_SOURCE_BLOB", blob_sha):
            self.assertEqual(op1_otp.fetch_public_otp_key(fake_opener), self.key)
        self.assertEqual(calls, [(op1_otp.OTP_SOURCE_URL, 15)])
        self.assertIn(op1_otp.OTP_SOURCE_COMMIT, calls[0][0])

    def test_reject_tampered_download(self):
        with self.assertRaisesRegex(ValueError, "pinned Git blob"):
            op1_otp.fetch_public_otp_key(lambda req, timeout: io.BytesIO(b"tampered"))

    def test_reject_oversize_download(self):
        with self.assertRaisesRegex(ValueError, "size limit"):
            op1_otp.fetch_public_otp_key(
                lambda req, timeout: io.BytesIO(b"x" * (op1_otp.MAX_DOWNLOAD_SIZE + 1))
            )

    def test_encrypted_ldr_roundtrip(self):
        plain_first = b"abcdefgh"
        rest = b"0123456789abcdef"
        ciphertext = _encrypt_xtea(plain_first, self.key)
        payload = ciphertext + rest
        flags = 0x6400  # FIRST + CALLBACK + INDIRECT
        encrypted = _ldr_header(flags, 24) + payload
        result = op1_otp.decrypt_ldr(encrypted, self.key)
        self.assertEqual(result[16:], plain_first + rest)
        self.assertEqual(struct.unpack_from("<H", result)[0], 0x4000)
        checksum = 0
        for byte in result[:16]:
            checksum ^= byte
        self.assertEqual(checksum, 0)

    def test_invalid_ldr_header_rejected(self):
        with self.assertRaisesRegex(ValueError, "Invalid LDR"):
            op1_otp.decrypt_ldr(b"\0" * 16, self.key)

    def test_unencrypted_only_rejected(self):
        with self.assertRaisesRegex(ValueError, "No encrypted"):
            op1_otp.decrypt_ldr(_ldr_header(0x100, 64), self.key)


if __name__ == "__main__":
    unittest.main()
