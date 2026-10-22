"""End-to-end SPP regressions with 18 seconds/channel of real MATLAB fixtures."""
import csv
import gzip
import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

EXECUTABLE = str(Path(sys.argv.pop(1)).resolve())
FIXTURE_PATH = Path(sys.argv.pop(1))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compare_b1c_position import compare_values, rows, SOLUTION_FIELDS, OBSERVATION_FIELDS  # noqa: E402
FIXTURE = json.loads(gzip.decompress(FIXTURE_PATH.read_bytes()))


class PositionCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.tracking = self.root / "tracking"
        self.navigation = self.root / "navigation"
        self.output = self.root / "position"
        self.tracking.mkdir()
        self.navigation.mkdir()
        self.write_fixture()

    def tearDown(self):
        self.temp.cleanup()

    def write_fixture(self, select=None, tail=False):
        for prn, records in FIXTURE["tracking"].items():
            if select and int(prn) not in select:
                continue
            with (self.tracking / f"prn_{int(prn):02d}.csv").open("w", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(FIXTURE["tracking_columns"])
                writer.writerows(records)
                if tail:
                    last = records[-1]
                    sample = int(last[2]) + int(last[3])
                    for j in range(200):
                        # Loss, then a new generation without any new nav
                        # frame: neither interval may use the old anchor.
                        epoch = int(last[0]) + j + 1 if j < 75 else j - 75
                        generation = int(last[1]) + (j >= 75)
                        writer.writerow([epoch, generation, sample, 180000, .01,
                                         1023000, 0, 1, 1,
                                         "lost" if j < 75 else "locked", int(j >= 75)])
                        sample += 180000
        with (self.navigation / "frames.csv").open("w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(FIXTURE["navigation_columns"])
            writer.writerows(r for r in FIXTURE["navigation_frames"] if not select or int(r[0]) in select)

    def run_cli(self, *extra, success=True):
        result = subprocess.run([EXECUTABLE, "--tracking-dir", str(self.tracking),
                                 "--navigation-dir", str(self.navigation), "--sample-rate", "18000000",
                                 "--output-dir", str(self.output), *extra], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stderr)
        return result

    def test_all_original_matlab_epochs_and_observations(self):
        self.run_cli()
        actual, obs = rows(self.output / "solutions.csv"), rows(self.output / "observations.csv")
        compare_values(actual, FIXTURE["solutions"], SOLUTION_FIELDS)
        compare_values(obs, FIXTURE["observations"], OBSERVATION_FIELDS)
        self.assertEqual(len(actual), 90)
        self.assertTrue(all(r["valid"] == "1" for r in actual))
        self.assertEqual(actual[0]["satellites"], "7")
        self.assertTrue(all(r["satellites"] == "6" for r in actual[1:]))
        excluded = rows(self.output / "exclusions.csv")
        self.assertTrue(all(r["prn"] == "25" and r["reason"] == "below_elevation_mask" for r in excluded))

    def test_initial_clock_guess_is_solved_not_assumed_range(self):
        self.run_cli("--start-offset-ms", "120")
        actual = rows(self.output / "solutions.csv")
        for a, ref in zip(actual, FIXTURE["solutions"]):
            self.assertEqual(a["valid"], "1")
            for k in ("x_m", "y_m", "z_m"):
                self.assertLess(abs(float(a[k]) - float(ref[k])), .05)
        expected_bias = float(FIXTURE["solutions"][0]["clock_correction_m"]) + (120 - 68.802) * 299792.458
        self.assertLess(abs(float(actual[0]["clock_correction_m"]) - expected_bias), .05)

    def test_lost_and_reacquired_tracking_needs_new_frame_anchor(self):
        self.write_fixture(tail=True)
        self.run_cli()
        actual = rows(self.output / "solutions.csv")
        self.assertGreater(len(actual), 90)
        self.assertTrue(all(r["valid"] == "0" for r in actual[90:]))
        self.assertTrue(all(math.isnan(float(r["x_m"])) for r in actual[90:]))
        reasons = {r["reason"] for r in rows(self.output / "exclusions.csv")}
        self.assertIn("tracking_unavailable", reasons)
        self.assertIn("no_continuous_anchor", reasons)

    def test_stale_ephemeris_and_satellite_shortage_never_fabricate_fixes(self):
        self.run_cli("--max-ephemeris-age", "1")
        self.assertTrue(all(r["valid"] == "0" for r in rows(self.output / "solutions.csv")))
        run = json.loads((self.output / "run.json").read_text())
        self.assertTrue(run["complete"])
        self.assertEqual(run["valid_fixes"], 0)
        for file in self.tracking.glob("*.csv"):
            file.unlink()
        self.write_fixture(select={23, 27, 28})
        self.output = self.root / "shortage"
        self.run_cli()
        self.assertEqual(rows(self.output / "solutions.csv"), [])

    def test_frame_tracking_identity_and_crc_rejection(self):
        path = self.tracking / "prn_23.csv"
        text = path.read_text()
        lines = text.splitlines()
        cells = lines[20].split(",")
        cells[7] = str(-float(cells[7]))  # actual data hard symbol mismatch
        lines[20] = ",".join(cells)
        path.write_text("\n".join(lines) + "\n")
        self.run_cli(success=False)
        self.assertFalse(self.output.exists())
        path.write_text(text)
        nav = self.navigation / "frames.csv"
        data = rows(nav)
        data[0]["bits"] = data[0]["bits"][:100] + str(1 - int(data[0]["bits"][100])) + data[0]["bits"][101:]
        with nav.open("w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=FIXTURE["navigation_columns"])
            writer.writeheader()
            writer.writerows(data)
        self.run_cli(success=False)

    def test_unhealthy_crc_valid_frames_are_excluded(self):
        data = rows(self.navigation / "frames.csv")
        for frame in data:
            bits = [int(v) for v in frame["bits"]]
            bits[620] = bits[621] = 1  # HS=3; recompute CRC and received signs.
            remainder = 0
            for bit in bits[614:854] + [0] * 24:
                remainder = (remainder << 1) | bit
                if remainder & (1 << 24):
                    remainder ^= 0x1864CFB
            bits[854:878] = [(remainder >> (23 - k)) & 1 for k in range(24)]
            frame["bits"] = "".join(map(str, bits))
            path = self.tracking / f"prn_{int(frame['prn']):02d}.csv"
            track = rows(path)
            for k, bit in enumerate(bits[614:878]):
                position = 72 + 36 * (k % 48) + 2 + 3 * (k // 48)
                track[position]["data_i_p"] = "10" if bit ^ int(frame["inverted"]) else "-10"
            with path.open("w", newline="") as f:
                writer = csv.DictWriter(f, fieldnames=FIXTURE["tracking_columns"])
                writer.writeheader()
                writer.writerows(track)
        with (self.navigation / "frames.csv").open("w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=FIXTURE["navigation_columns"])
            writer.writeheader()
            writer.writerows(data)
        self.run_cli()
        self.assertTrue(all(r["valid"] == "0" for r in rows(self.output / "solutions.csv")))
        self.assertTrue(all(r["reason"] == "health_or_integrity" for r in rows(self.output / "exclusions.csv")))

    def test_options_tropo_and_output_protection(self):
        self.run_cli("--no-tropo")
        self.assertTrue(all(r["valid"] == "1" for r in rows(self.output / "solutions.csv")))
        self.assertTrue(all(float(r["troposphere_m"]) == 0 for r in rows(self.output / "observations.csv")))
        before = (self.output / "solutions.csv").read_bytes()
        self.run_cli(success=False)
        self.assertEqual(before, (self.output / "solutions.csv").read_bytes())
        for name, value in [("--sample-rate", "nan"), ("--sample-rate", "18000001"),
                            ("--period-ms", "0"), ("--period-ms", "1.000001"),
                            ("--elevation-mask", "90"), ("--start-offset-ms", "-1"),
                            ("--max-ephemeris-age", "inf")]:
            with self.subTest(name=name, value=value):
                self.output = self.root / (name[2:] + value)
                self.run_cli(name, value, success=False)


if __name__ == "__main__":
    unittest.main()
