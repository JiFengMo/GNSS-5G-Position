"""Synthetic transport tests; none of these tests open a USRP."""
import contextlib
import csv
import ctypes
from ctypes.util import find_library
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import capture_iq
from uhd_iq import UHDReceiver, StreamArgs, StreamCommand, TuneRequest, TuneResult


class FakeRadio:
    """Explicit test injection; production CLI has no synthetic mode."""
    def __init__(self, *, fail_at=None, gap=0, stop_after=None, fail_open=False):
        self.fail_at, self.gap, self.stop_after, self.fail_open = fail_at, gap, stop_after, fail_open
        self.calls = self.count = 0
        self.closed = self.started = False

    def open(self, settings, stop):
        self.stop = stop
        if self.fail_open:
            raise RuntimeError("TEST_ONLY_DEVICE_NOT_FOUND")
        self.fs = 1000000.25
        return {"sample_rate_hz": self.fs, "center_freq_hz": settings["center_freq_hz"],
                "board": "TEST_ONLY", "uhd_version": "TEST_ONLY"}

    def start(self):
        self.started = True

    def receive(self, want):
        self.calls += 1
        if self.calls == self.fail_at:
            if not self.gap:
                raise RuntimeError("TEST_ONLY_RX_METADATA_OVERFLOW")
            self.count += self.gap
        n = min(want, 997)  # Partial reads, including a short final block.
        full, fraction = 10000000000, self.count / self.fs
        self.count += n
        if self.calls == self.stop_after:
            self.stop.set()
        return b"\x01\x00\xfe\xff" * n, full, fraction

    def close(self):
        self.closed = True


class IQCaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="raw iq test ")
        self.root = Path(self.temp.name)
        self.config = self.root / "capture config.f"
        self.source = (capture_iq.ROOT / "config/5G/iq_capture.f").read_text(encoding="utf-8")
        self.source = self.source.replace("duration_s = 3", "duration_s = 0.02")
        self.source = self.source.replace("sample_rate_hz = 30720000", "sample_rate_hz = 1000000")
        self.config.write_text(self.source, encoding="utf-8")
        self.job, _, self.text = capture_iq.settings_from_file(self.config, self.root)

    def tearDown(self):
        self.temp.cleanup()

    def run_capture(self, radio=None, stop=None):
        radio = radio or FakeRadio()
        destination = self.root / "run"
        with contextlib.redirect_stdout(io.StringIO()), patch.object(capture_iq, "ensure_space"):
            code, result = capture_iq.capture(self.job, destination, self.text,
                                              stop or threading.Event(), lambda: radio)
        self.assertTrue(radio.closed)
        self.assertEqual(result, json.loads((destination / "capture.json").read_text(encoding="utf-8")))
        self.assertFalse(result["absolute_utc_known"])
        return code, result, destination

    def test_exact_iq_bytes_actual_rate_large_counters_and_packet_index(self):
        code, result, directory = self.run_capture()
        self.assertEqual(code, 0)
        self.assertTrue(result["complete"] and result["continuous_prefix"])
        self.assertEqual(result["samples_written"], 20000)
        self.assertEqual((directory / "capture.c16").read_bytes(), b"\x01\x00\xfe\xff" * 20000)
        self.assertEqual(result["actual"]["sample_rate_hz"], 1000000.25)
        self.assertGreater(int(result["first_hardware_sample"]), 2**53)
        with (directory / "blocks.csv").open(newline="") as fp:
            rows = list(csv.DictReader(fp))
        offset, expected = 0, int(result["first_hardware_sample"])
        for row in rows:
            self.assertEqual(int(row["file_offset_bytes"]), offset * 4)
            self.assertEqual(int(row["hardware_sample"]), expected)
            offset += int(row["samples"]); expected += int(row["samples"])
        self.assertEqual(offset, result["samples_written"])
        self.assertEqual((directory / "config.f").read_text(encoding="utf-8"), self.text)
        self.assertIn("fs_hz=1000000.25", (directory / "capture.meta.txt").read_text())

    def test_gap_stops_before_concatenating_discontinuous_data(self):
        code, result, directory = self.run_capture(FakeRadio(fail_at=3, gap=7))
        self.assertEqual(code, 1)
        self.assertFalse(result["complete"])
        self.assertTrue(result["continuous_prefix"])
        self.assertEqual(result["samples_written"], 1994)
        self.assertEqual((directory / "capture.c16").stat().st_size, 1994 * 4)
        self.assertIn("间断", result["error"])

    def test_rx_overflow_does_not_claim_a_complete_recording(self):
        code, result, _ = self.run_capture(FakeRadio(fail_at=2))
        self.assertEqual(code, 1)
        self.assertEqual(result["status"], "failed")
        self.assertFalse(result["complete"])
        self.assertEqual(result["samples_written"], 997)
        self.assertIn("OVERFLOW", result["error"])

    def test_user_interrupt_flushes_partial_capture_and_closes_device(self):
        code, result, directory = self.run_capture(FakeRadio(stop_after=2))
        self.assertEqual(code, 130)
        self.assertEqual(result["status"], "interrupted")
        self.assertEqual(result["samples_written"], 1994)
        self.assertTrue(result["continuous_prefix"])
        self.assertEqual((directory / "capture.c16").stat().st_size, 1994 * 4)

    def test_startup_failure_leaves_diagnostics_without_fake_samples(self):
        code, result, directory = self.run_capture(FakeRadio(fail_open=True))
        self.assertEqual(code, 1)
        self.assertFalse(result["complete"] or result["continuous_prefix"])
        self.assertEqual(result["samples_written"], 0)
        self.assertFalse((directory / "capture.c16").exists())
        self.assertIn("TEST_ONLY_DEVICE_NOT_FOUND", result["error"])

    def test_writer_failure_is_reported_and_does_not_deadlock(self):
        def bad_writer(writer):
            writer.error = OSError("TEST_ONLY_DISK_FULL")
        with patch.object(capture_iq.DiskWriter, "work", bad_writer):
            code, result, _ = self.run_capture()
        self.assertEqual(code, 1)
        self.assertFalse(result["complete"] or result["continuous_prefix"])
        self.assertIn("DISK_FULL", result["error"])

    def test_full_queue_preserves_failure(self):
        with patch.object(capture_iq.DiskWriter, "write", side_effect=RuntimeError("TEST_ONLY_QUEUE_FULL")):
            code, result, _ = self.run_capture()
        self.assertEqual(code, 1)
        self.assertFalse(result["complete"])
        self.assertIn("QUEUE_FULL", result["error"])

    def test_slow_disk_fills_real_bounded_queue_without_silent_loss(self):
        directory = self.root / "slow disk"; directory.mkdir()
        release = threading.Event()
        original = capture_iq.DiskWriter.work
        def paused(writer):
            release.wait(5)
            original(writer)
        with patch.object(capture_iq.DiskWriter, "work", paused):
            writer = capture_iq.DiskWriter(directory, 2)
        try:
            writer.write(b"\x01\x00\x02\x00", 0, 0, 0)
            writer.write(b"\x03\x00\x04\x00", 0, .000001, 1)
            with self.assertRaisesRegex(RuntimeError, "队列已满"):
                writer.write(b"\x05\x00\x06\x00", 0, .000002, 2)
        finally:
            release.set()
            writer.finish()
        self.assertEqual(writer.samples, 2)
        self.assertEqual((directory / "capture.c16").read_bytes(), b"\x01\x00\x02\x00\x03\x00\x04\x00")

    def test_recording_directory_is_never_overwritten(self):
        destination = self.root / "run"
        destination.mkdir()
        saved = destination / "capture.c16"; saved.write_bytes(b"keep")
        with self.assertRaises(FileExistsError):
            capture_iq.capture(self.job, destination, self.text, threading.Event(), FakeRadio)
        self.assertEqual(saved.read_bytes(), b"keep")

    def test_strict_config_rejects_typoes_nonfinite_limits_and_path_escape(self):
        changes = [("duration_s = 0.02", "duration_s = nan"), ("duration_s = 0.02", "duration_s = 0"),
                   ("block_samples = 262144", "block_samples = 12.5"),
                   ("serial = 2512531", "serial = bad,serial=other"),
                   ("record_dir = iq-capture", "record_dir = ../outside"),
                   ("clock_source = internal", "clock_source = unknown"),
                   ("ssb_scs_khz = 15", "ssb_scs_khz = 20"),
                   ("target_pci = 84", "target_pci = 1008"),
                   ("device_args = type=b200", "device_args = type=b200,serial=other")]
        for old, new in changes:
            with self.subTest(new=new):
                self.config.write_text(self.source.replace(old, new), encoding="utf-8")
                with self.assertRaises(ValueError):
                    capture_iq.settings_from_file(self.config, self.root)
        for suffix in ("\nunknown=1\n", "\nserial=other\n"):
            self.config.write_text(self.source + suffix, encoding="utf-8")
            with self.assertRaises(ValueError):
                capture_iq.settings_from_file(self.config, self.root)

    def test_config_check_does_not_create_capture_or_open_device(self):
        with patch.object(capture_iq, "UHDReceiver", side_effect=AssertionError("must not open")), \
             patch.object(capture_iq, "capture", side_effect=AssertionError("must not capture")), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(capture_iq.main(["--check", "--config", str(self.config)]), 0)
        self.assertFalse((self.root / "run").exists())

    def test_disk_budget_and_invalid_timestamps(self):
        with patch.object(capture_iq.shutil, "disk_usage", return_value=type("Disk", (), {"free": 1})()):
            with self.assertRaises(OSError):
                capture_iq.ensure_space(self.root, 100)
        self.assertEqual(capture_iq.sample_tick(10000000000, .5, 1000000.25), 10000002500500000)
        for fraction in (-.1, 1, float("nan")):
            with self.assertRaises(RuntimeError):
                capture_iq.sample_tick(0, fraction, 1000000.25)

    @unittest.skipUnless(os.name == "posix", "shell launcher runs on Linux")
    def test_shell_capture_check_and_help_with_space_in_config_path(self):
        for suffix in (["--capture-iq", "--check", "--config", str(self.config)], ["--help"], ["--capture-iq", "--help"]):
            result = subprocess.run(["sh", str(capture_iq.ROOT / "run_5G_only.sh"), *suffix],
                                    cwd=self.root, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run(["sh", str(capture_iq.ROOT / "run_5G_only.sh"), "--capture-iq", "--ui-only"],
                                capture_output=True, timeout=15)
        self.assertNotEqual(result.returncode, 0)

    @unittest.skipUnless(os.name == "posix" and find_library("uhd"), "requires installed UHD C runtime, no SDR needed")
    def test_real_uhd_c_symbols_and_handle_lifecycle_without_device(self):
        receiver = UHDReceiver()
        self.assertTrue(receiver.text("get_version_string"))
        handle = ctypes.c_void_p()
        receiver.call("rx_metadata_make", ctypes.byref(handle))
        receiver.call("rx_metadata_free", ctypes.byref(handle))
        receiver.close()
        if ctypes.sizeof(ctypes.c_void_p) == 8:
            self.assertEqual([ctypes.sizeof(cls) for cls in (TuneRequest, TuneResult, StreamArgs, StreamCommand)], [48, 40, 40, 40])


if __name__ == "__main__":
    unittest.main()
