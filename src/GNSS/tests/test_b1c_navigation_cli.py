"""Navigation CLI integration tests using small original MATLAB fixtures."""
import csv
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())
FIXTURE_PATH = Path(sys.argv.pop(1))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compare_b1c_navigation import compare_fields, read_csv  # noqa: E402

FIXTURE = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))["frames"]
HEADER = ["epoch", "generation", "absolute_sample", "sample_count", "integration_s",
          "data_i_p", "pilot_q_p", "state", "valid"]
BASE = 2**53 + 123


def write_tracking(directory, frames, edit=None, limit=None, invert=False):
    directory.mkdir()
    grouped = {}
    for frame in frames:
        grouped.setdefault(frame["prn"], []).append(frame)
    for prn, items in grouped.items():
        with (directory / f"prn_{prn:02d}.csv").open("w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(HEADER)
            for block, frame in enumerate(items):
                for j, (data, pilot) in enumerate(zip(frame["symbols"], frame["secondary_bits"])):
                    index = block * 1800 + j
                    if limit is not None and index >= limit:
                        break
                    sign = -1 if invert else 1
                    row = [index, frame["generation"], BASE + 10 * index, 10, .01,
                           sign * (11 if data == "1" else -11),
                           sign * (100 if pilot == "0" else -100), "locked", 1]
                    if edit:
                        edit(index, row)
                    writer.writerow(row)


class NavigationCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.tracking = self.root / "tracking"
        self.output = self.root / "navigation"

    def tearDown(self):
        self.temp.cleanup()

    def run_cli(self, *extra, success=True):
        result = subprocess.run([EXECUTABLE, "--tracking-dir", str(self.tracking),
                                 "--sample-rate", "1000", "--output-dir", str(self.output),
                                 *extra], capture_output=True, text=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stderr)
        return result

    def test_original_matlab_frames_fields_and_large_sample_indices(self):
        write_tracking(self.tracking, FIXTURE)
        self.run_cli()
        frames = read_csv(self.output / "frames.csv")
        ephemerides = read_csv(self.output / "ephemerides.csv")
        self.assertEqual(len(frames), len(FIXTURE))
        self.assertEqual(len(ephemerides), len(FIXTURE))
        grouped = {}
        for ref in FIXTURE:
            grouped.setdefault(ref["prn"], []).append(ref)
        expected = [(prn, block, ref) for prn, items in sorted(grouped.items())
                    for block, ref in enumerate(items)]
        for (prn, block, ref), frame, eph in zip(expected, frames, ephemerides):
            self.assertEqual(int(frame["prn"]), prn)
            self.assertEqual(frame["status"], "ok")
            self.assertEqual(frame["bits"], ref["bits"])
            self.assertEqual(int(frame["absolute_sample"]), BASE + block * 18000)
            self.assertEqual(int(frame["end_sample"]), BASE + (block + 1) * 18000)
            self.assertEqual(compare_fields(eph, ref), 0.0)
        run = json.loads((self.output / "run.json").read_text())
        self.assertTrue(run["complete"])
        self.assertFalse(run["ldpc_error_correction"])

    def test_both_carrier_polarities(self):
        write_tracking(self.tracking, [FIXTURE[0]], invert=True)
        self.run_cli()
        frame, = read_csv(self.output / "frames.csv")
        self.assertEqual(frame["bits"], FIXTURE[0]["bits"])
        self.assertEqual(int(frame["inverted"]), 1 - FIXTURE[0]["inverted"])
        self.assertEqual(int(frame["secondary_correlation"]), -1800)

    def test_bch_and_crc_failures_emit_diagnostics_without_ephemeris(self):
        for symbol, status in [(0, "bch21_failed"), (21, "bch51_failed"),
                               (72, "crc2_failed"), (74, "crc3_failed")]:
            with self.subTest(status=status), tempfile.TemporaryDirectory() as tmp:
                self.tracking = Path(tmp) / "tracking"
                self.output = Path(tmp) / "navigation"
                def edit(index, row):
                    if index == symbol:
                        row[5] *= -1
                write_tracking(self.tracking, [FIXTURE[0]], edit=edit)
                self.run_cli()
                frame, = read_csv(self.output / "frames.csv")
                self.assertEqual(frame["status"], status)
                self.assertEqual(frame["valid"], "0")
                self.assertEqual(frame["bits"], "")
                self.assertEqual(read_csv(self.output / "ephemerides.csv"), [])

    def test_no_frames_from_partial_loss_gap_reset_or_bad_secondary(self):
        for fault in ("partial", "invalid", "sample_gap", "epoch_gap", "generation", "pilot"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as tmp:
                self.tracking = Path(tmp) / "tracking"
                self.output = Path(tmp) / "navigation"
                def edit(index, row):
                    if index == 900:
                        if fault == "invalid":
                            row[7], row[8] = "lost", 0
                        elif fault == "sample_gap":
                            row[2] += 1
                        elif fault == "epoch_gap":
                            row[0] += 1
                        elif fault == "generation":
                            row[1] += 1
                        elif fault == "pilot":
                            row[6] *= -1
                write_tracking(self.tracking, [FIXTURE[0]], edit=edit,
                               limit=1799 if fault == "partial" else None)
                self.run_cli()
                self.assertEqual(read_csv(self.output / "frames.csv"), [])

    def test_reference_systematic_parity_scope(self):
        # SF2 parity begins at row 18, column 24 in this interleaver. It is
        # deliberately unused, matching MATLAB; this is not a full FEC test.
        def edit(index, row):
            if index == 72 + 36 * 24 + 18:
                row[5] *= -1
        write_tracking(self.tracking, [FIXTURE[0]], edit=edit)
        self.run_cli()
        frame, = read_csv(self.output / "frames.csv")
        self.assertEqual(frame["status"], "ok")
        self.assertEqual(frame["bits"], FIXTURE[0]["bits"])

    def test_invalid_input_and_output_protection(self):
        write_tracking(self.tracking, [FIXTURE[0]])
        self.run_cli()
        original = (self.output / "frames.csv").read_bytes()
        self.run_cli(success=False)
        self.assertEqual((self.output / "frames.csv").read_bytes(), original)
        for option in ("nan", "inf", "0", "-1", "1001", "junk"):
            with self.subTest(option=option):
                self.output = self.root / f"invalid-{option}"
                self.run_cli("--sample-rate", option, success=False)
        for fault in ("nan", "bad_state", "empty", "large_integer", "negative_sample", "valid_state"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as tmp:
                self.tracking = Path(tmp) / "tracking"
                self.output = Path(tmp) / "navigation"
                def edit(index, row):
                    if index == 900:
                        column, value = {"nan": (5, "nan"), "bad_state": (7, "unknown"),
                                         "empty": (6, ""), "large_integer": (2, 2**64),
                                         "negative_sample": (2, -1), "valid_state": (7, "lost")}[fault]
                        row[column] = value
                write_tracking(self.tracking, [FIXTURE[0]], edit=edit)
                self.run_cli(success=False)
                self.assertFalse(json.loads((self.output / "run.json").read_text())["complete"])


if __name__ == "__main__":
    unittest.main()
