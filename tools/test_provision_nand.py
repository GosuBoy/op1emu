"""Unit tests for the OP-1 NAND provisioner; no firmware bytes needed."""

import io
import lzma
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest
import zlib
import sys

sys.path.insert(0, str(Path(__file__).parent))
from provision_nand import PAGE_SIZE, OOB_SIZE, PAGES_PER_BLOCK, provision, unpack_op1


class ProvisionTests(unittest.TestCase):
    def sample(self, path, seq=0x1000):
        spare = b"\xff" * 2 + struct.pack("<I", seq) + b"\xab" * 24 + b"\xff" * 34
        path.write_bytes(b"A" * PAGE_SIZE + spare + b"B" * PAGE_SIZE + spare)

    def test_interleaved_to_split_oob(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            self.sample(root / "input")
            count = provision(root / "input", root / "nand", start_block=1, total_blocks=3)
            self.assertEqual(count, 2)
            raw = (root / "nand").read_bytes()
            page = PAGES_PER_BLOCK
            self.assertEqual(len(raw), 3 * PAGES_PER_BLOCK * (PAGE_SIZE + OOB_SIZE))
            self.assertEqual(raw[page * PAGE_SIZE:(page + 1) * PAGE_SIZE], b"A" * PAGE_SIZE)
            off = 3 * PAGES_PER_BLOCK * PAGE_SIZE + page * OOB_SIZE
            self.assertEqual(raw[off:off + OOB_SIZE], b"\xff" * 4 +
                             struct.pack("<I", 0x1000) + b"\xab" * 24 + b"\xff" * 32)
            self.assertEqual(raw[:PAGE_SIZE], b"\xff" * PAGE_SIZE)

    def test_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            self.sample(root / "input")
            (root / "nand").write_bytes(b"keep")
            with self.assertRaises(FileExistsError):
                provision(root / "input", root / "nand", 1, 3)
            self.assertEqual((root / "nand").read_bytes(), b"keep")

    def test_bad_tags_remove_partial_output(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            self.sample(root / "input", seq=0x21)
            with self.assertRaisesRegex(ValueError, "OOB"):
                provision(root / "input", root / "nand", 1, 3)
            self.assertFalse((root / "nand").exists())

    def test_op1_crc_archive_extract(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            stream = io.BytesIO()
            with tarfile.open(fileobj=stream, mode="w") as tf:
                data = b"test data"
                info = tarfile.TarInfo("content/op1.db")
                info.size = len(data)
                tf.addfile(info, io.BytesIO(data))
            compressed = lzma.compress(stream.getvalue(), format=lzma.FORMAT_ALONE)
            (root / "pkg.op1").write_bytes(struct.pack("<I", zlib.crc32(compressed)) + compressed)
            unpack_op1(root / "pkg.op1", root / "root")
            self.assertEqual((root / "root/content/op1.db").read_bytes(), b"test data")

    def test_op1_rejects_crc_corruption(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            (root / "pkg.op1").write_bytes(b"\0" * 4 + b"X" * 20)
            with self.assertRaisesRegex(ValueError, "CRC32"):
                unpack_op1(root / "pkg.op1", root / "root")


if __name__ == "__main__":
    unittest.main()
