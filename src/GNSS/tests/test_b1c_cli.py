"""Offline reader/CLI regressions; C DSP tests cover the correlation itself."""
import csv
import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

RECEIVER = str(Path(sys.argv[1]).resolve())
COUNT = 81840


class ReceiverCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.iq = self.root / "window.iq"
        self.iq.write_bytes(b"\0\0" * COUNT)

    def run_receiver(self, *arguments):
        return subprocess.run([RECEIVER, "--input", str(self.iq), "--input-format", "ci8",
                               "--sample-rate", "4092000", "--prn", "23", *arguments],
                              capture_output=True, text=True, timeout=20)

    def test_help_and_bad_arguments(self):
        self.assertEqual(subprocess.run([RECEIVER, "--help"], capture_output=True).returncode, 0)
        for arguments in [("--sample-rate", "nan"), ("--sample-rate", "4092000junk"),
                          ("--sample-rate", "-1"), ("--prn", "64"), ("--prns", "1,1"),
                          ("--prns", "2-1"), ("--prns", "1,"), ("--skip-samples", "-1"),
                          ("--skip-samples", "18446744073709551616"),
                          ("--input-format", "int8"), ("--unknown", "1"), ("--prn",),
                          ("--threads", "0"), ("--threads", "64"), ("--threads", "1.5")]:
            with self.subTest(arguments=arguments):
                self.assertEqual(self.run_receiver(*arguments).returncode, 2)

    def test_zero_ci8_and_cf32(self):
        ci8 = self.run_receiver()
        self.assertEqual(ci8.returncode, 7)
        self.iq.write_bytes(b"\0" * (8 * COUNT))
        cf32 = self.run_receiver("--input-format", "cf32")
        self.assertEqual(cf32.returncode, 7)
        self.assertEqual(ci8.stdout, cf32.stdout)
        row = next(csv.DictReader(io.StringIO(ci8.stdout)))
        self.assertEqual(row["peak_metric"], "0")
        self.assertEqual(row["cn0_db_hz"], "nan")
        self.assertEqual(row["acquired"], "0")

    def test_sample_offset_and_bounded_read(self):
        self.iq.write_bytes(b"\x80\x7f" * 13 + b"\0\0" * COUNT + b"unused tail")
        result = self.run_receiver("--skip-samples", "13")
        self.assertEqual(result.returncode, 7)
        row = next(csv.DictReader(io.StringIO(result.stdout)))
        self.assertEqual(row["input_start_sample"], "13")
        self.assertEqual(row["peak_metric"], "0")
        self.assertEqual(self.run_receiver("--skip-samples", str(1 << 62)).returncode, 3)

    def test_short_window(self):
        self.iq.write_bytes(b"\0\0" * (COUNT - 1))
        self.assertEqual(self.run_receiver().returncode, 3)

    def test_nonfinite_cf32(self):
        for value in (float("nan"), float("inf")):
            self.iq.write_bytes(struct.pack("<ff", 0, value) + b"\0" * (8 * (COUNT - 1)))
            self.assertEqual(self.run_receiver("--input-format", "cf32").returncode, 6)

    def test_invalid_grid(self):
        for arguments in [("--doppler-step", "0"), ("--fine-step", "30"),
                          ("--coherent-ms", "11"), ("--threshold", "0")]:
            with self.subTest(arguments=arguments):
                self.assertEqual(self.run_receiver(*arguments).returncode, 6)

    def test_output_file(self):
        output = self.root / "capture.csv"
        self.assertEqual(self.run_receiver("--output", str(output)).returncode, 7)
        with output.open() as stream:
            self.assertEqual(len(list(csv.DictReader(stream))), 1)
        original = self.iq.read_bytes()
        self.assertEqual(self.run_receiver("--output", str(self.iq)).returncode, 2)
        self.assertEqual(self.iq.read_bytes(), original)
        self.assertEqual(self.run_receiver("--output", str(self.root / "absent" / "x.csv")).returncode, 3)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
