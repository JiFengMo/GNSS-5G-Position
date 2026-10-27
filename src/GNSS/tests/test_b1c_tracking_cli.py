"""Tracking file/metadata and automatic reacquisition tests; no NumPy required."""
import csv
import json
import math
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

TRACKER = str(Path(sys.argv[1]).resolve())
GENERATOR = str(Path(sys.argv[2]).resolve())


def read_csv(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


class TrackingCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.iq = self.root / "zero.iq"
        self.iq.write_bytes(b"\0\0" * 220000)
        self.seeds = self.root / "seeds.csv"
        self.seeds.write_text(
            "prn,carrier_hz,doppler_hz,code_phase_samples,peak_metric,cn0_db_hz,acquired,coarse_doppler_hz,fine_metric,input_start_sample\n"
            "23,20000,0,0,50,nan,1,0,1,0\n"
            "37,20000,0,100,50,nan,1,0,1,0\n", encoding="utf-8")

    def run_track(self, output, *args):
        return subprocess.run([TRACKER, "--input", str(self.iq), "--input-format", "ci8",
                               "--acquisition-csv", str(self.seeds), "--sample-rate", "4092000",
                               "--if", "20000", "--seconds", ".03", "--no-reacquire",
                               "--output-dir", str(output), *args],
                              capture_output=True, text=True, timeout=120)

    def test_shared_reader_chunk_invariance_and_formats(self):
        a, b, c = [self.root / name for name in ("a", "b", "c")]
        self.assertEqual(self.run_track(a, "--chunk-samples", "777").returncode, 0)
        self.assertEqual(self.run_track(b, "--chunk-samples", "65536").returncode, 0)
        self.iq.write_bytes(b"\0" * (8 * 220000))
        self.assertEqual(self.run_track(c, "--input-format", "cf32").returncode, 0)
        for prn in (23, 37):
            filename = f"prn_{prn:02d}.csv"
            self.assertEqual((a / filename).read_bytes(), (b / filename).read_bytes())
            self.assertEqual((a / filename).read_bytes(), (c / filename).read_bytes())
            rows = read_csv(a / filename)
            self.assertEqual(len(rows), 3)
            self.assertTrue(all(row["valid"] == "0" for row in rows))
            self.assertEqual(int(rows[0]["absolute_sample"]), 0 if prn == 23 else 100)
        metadata = json.loads((a / "run.json").read_text())
        self.assertTrue(metadata["complete"])

    def test_short_input_does_not_emit_partial_epoch(self):
        self.iq.write_bytes(b"\0\0" * 40919)
        out = self.root / "short"
        self.assertEqual(self.run_track(out, "--prns", "23").returncode, 3)
        self.assertEqual(read_csv(out / "prn_23.csv"), [])
        summary = read_csv(out / "summary.csv")[0]
        self.assertEqual(summary["pending_samples"], "40919")
        self.assertEqual(summary["complete"], "0")
        self.assertFalse(json.loads((out / "run.json").read_text())["complete"])

    def test_parallel_channels_keep_csv_and_event_order(self):
        reference=self.root/'serial';parallel=self.root/'parallel'
        self.assertEqual(self.run_track(reference,'--threads','1').returncode,0)
        self.assertEqual(self.run_track(parallel,'--threads','4').returncode,0)
        for name in ('prn_23.csv','prn_37.csv','events.csv','summary.csv'):
            self.assertEqual((reference/name).read_bytes(),(parallel/name).read_bytes(),name)

    def test_invalid_options_seeds_and_nonfinite(self):
        for arguments in [("--sample-rate", "nan"), ("--seconds", ".015"),
                          ("--chunk-samples", "0"), ("--pll-bandwidth", "19"),
                          ("--early-late", ".5"), ("--if", "0"),
                          ("--threads", "0"), ("--threads", "64"), ("--threads", "1.5")]:
            with self.subTest(arguments=arguments):
                self.assertEqual(self.run_track(self.root / "bad", *arguments).returncode, 2)
        self.iq.write_bytes(struct.pack("<ff", float("nan"), 0) + b"\0" * (8 * 219999))
        nonfinite = self.root / "nonfinite"
        self.assertEqual(self.run_track(nonfinite, "--input-format", "cf32").returncode, 6)
        self.assertFalse(json.loads((nonfinite / "run.json").read_text())["complete"])
        self.seeds.write_text("not a capture CSV\n")
        self.assertEqual(self.run_track(self.root / "bad_seed").returncode, 2)

    def test_signal_fade_and_frequency_jump_reacquisition(self):
        subprocess.run([GENERATOR, "--write-fixture", str(self.root)], check=True, timeout=120)
        self.iq = self.root / "synthetic.ci8"
        out = self.root / "recovered"
        run = self.run_track(out, "--seconds", "4", "--max-reacquisitions", "3")
        self.assertEqual(run.returncode, 0, run.stderr)
        rows = read_csv(out / "prn_23.csv")
        self.assertEqual(len(rows), 400)
        events = read_csv(out / "events.csv")
        successes = [row for row in events if row["event"] == "reacquire_ok"]
        self.assertTrue(successes, events)
        self.assertTrue(any(row["state"] == "lost" and row["valid"] == "0" for row in rows))
        self.assertEqual(rows[-1]["valid"], "1")
        self.assertEqual(rows[-1]["state"], "locked")
        self.assertLess(abs(float(rows[-1]["doppler_hz"]) - 180), 5)
        self.assertLess(abs(float(rows[-1]["residual_frequency_hz"])), 20)
        self.assertGreater(int(rows[-1]["generation"]), 0)
        for previous, current in zip(rows, rows[1:]):
            previous_end = int(previous["absolute_sample"]) + int(previous["sample_count"])
            if previous["generation"] == current["generation"]:
                self.assertEqual(int(current["absolute_sample"]), previous_end)
            else:
                self.assertGreaterEqual(int(current["absolute_sample"]), previous_end)
                self.assertEqual(current["state"], "pull_in")
                self.assertEqual(float(current["carrier_cycles"]), 0)
                self.assertEqual(current["valid"], "0")

    def test_parallel_reacquisition_has_private_readers_and_order(self):
        subprocess.run([GENERATOR,'--write-fixture',str(self.root)],check=True,timeout=120)
        self.iq=self.root/'synthetic.ci8'
        # A second seeded channel sees no matching signal and exercises failed
        # reacquisition alongside PRN 23's successful recovery in other tasks.
        with self.seeds.open('a') as stream:stream.write('37,20000,0,0,50,nan,1,0,1,0\n')
        directories=[self.root/'reacq-serial',self.root/'reacq-parallel']
        for threads,directory in zip((1,4),directories):
            run=self.run_track(directory,'--seconds','4','--max-reacquisitions','3','--threads',str(threads))
            self.assertEqual(run.returncode,0,run.stderr)
        for filename in ('prn_23.csv','prn_37.csv','events.csv','summary.csv'):
            self.assertEqual((directories[0]/filename).read_bytes(),(directories[1]/filename).read_bytes(),filename)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
