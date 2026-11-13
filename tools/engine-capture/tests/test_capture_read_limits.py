"""Capture parsing reads only the requested payload and bounded metadata."""

import io
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from test_invoke_engine_capture import runner


class BoundedReader(io.BytesIO):
    def __init__(self, value, limit, reads):
        super().__init__(value)
        self.limit = limit
        self.reads = reads

    def read(self, count=-1):
        if count < 0 or count > self.limit:
            raise AssertionError("Capture parser attempted an unbounded/oversized read")
        self.reads.append(count)
        return super().read(count)


class CaptureReadLimitTests(unittest.TestCase):
    def test_oversized_payload_is_refused_after_expected_size_plus_one(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            image = output / "capture.ppm"
            image.write_bytes(b"placeholder")
            header = b"P6\n64 64\n255\n"
            payload_bytes = 64 * 64 * 3
            reads = []
            # Simulate growth after metadata admission, without a large fixture.
            reader = BoundedReader(header + bytes(payload_bytes + 1000), payload_bytes + 1, reads)
            with mock.patch.object(Path, "open", return_value=reader):
                with self.assertRaisesRegex(runner.CaptureError, "payload"):
                    runner.validate_capture(output, {"camera": {"width": 64, "height": 64}})
            self.assertEqual(reads, [len(header), payload_bytes + 1])

    def test_wrong_header_is_refused_without_reading_pixels(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            (output / "capture.ppm").write_bytes(b"placeholder")
            header = b"P6\n64 64\n255\n"
            reads = []
            reader = BoundedReader(b"not an image" + bytes(20000), len(header), reads)
            with mock.patch.object(Path, "open", return_value=reader):
                with self.assertRaisesRegex(runner.CaptureError, "P6"):
                    runner.validate_capture(output, {"camera": {"width": 64, "height": 64}})
            self.assertEqual(reads, [len(header)])


if __name__ == "__main__":
    unittest.main()
