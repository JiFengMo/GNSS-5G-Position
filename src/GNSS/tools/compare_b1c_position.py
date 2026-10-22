"""Compare all SPP epochs and observations to a fresh original MATLAB run.

Run: python3 src/GNSS/tools/compare_b1c_position.py --reference MATLAB_DIR
--actual C_DIR --tracking TRACKING_DIR --decoder C_EXECUTABLE --output REPORT.
Outputs a JSON report; mismatch exits nonzero. Next: review residuals and
stability against antenna truth when available; see docs/positioning.md.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for data in iter(lambda: f.read(1048576), b""):
            h.update(data)
    return h.hexdigest()


def rows(path):
    with Path(path).open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def compare_values(actual, expected, fields):
    maxima = {}
    assert len(actual) == len(expected) > 0, (len(actual), len(expected))
    for a, b in zip(actual, expected):
        for name, tolerance in fields.items():
            x, y = float(a[name]), float(b[name])
            assert math.isfinite(x) and math.isfinite(y), (name, x, y)
            error = abs(x - y)
            assert error <= tolerance, (a.get("index"), a.get("prn"), name, x, y, error)
            maxima[name] = max(maxima.get(name, 0.0), error)
    return maxima


SOLUTION_FIELDS = dict(index=0, absolute_sample=0, local_time_before_s=1e-10, local_time_s=1e-10,
                       x_m=1e-5, y_m=1e-5, z_m=1e-5, clock_correction_m=1e-5,
                       latitude_deg=1e-9, longitude_deg=1e-9, height_m=1e-5,
                       gdop=1e-9, pdop=1e-9, tdop=1e-9, satellites=0)
OBSERVATION_FIELDS = dict(index=0, prn=0, absolute_sample=0, transmit_time_s=1e-10,
                          raw_pseudorange_m=1e-5, satellite_x_m=2e-6, satellite_y_m=2e-6,
                          satellite_z_m=2e-6, satellite_clock_s=2e-15,
                          clock_corrected_pseudorange_m=1e-5,
                          receiver_corrected_pseudorange_m=1e-5, elevation_deg=1e-9, azimuth_deg=1e-9)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("reference", "actual", "tracking", "decoder", "output"):
        p.add_argument(f"--{name}", type=Path, required=True)
    args = p.parse_args()
    manifest = json.loads((args.reference / "manifest.json").read_text(encoding="utf-8"))
    run = json.loads((args.actual / "run.json").read_text(encoding="utf-8"))
    assert run["complete"] and run["status_code"] == 0
    for name in ("sample_rate_hz", "period_ms", "elevation_mask_deg"):
        assert run[name] == manifest[name]
    assert run["troposphere"] == bool(manifest["troposphere"])
    inputs = {f.stem: sha256(f) for f in sorted(args.tracking.glob("prn_*.csv"))}
    if "tracking_mat" not in manifest["tracking_sha256"]:
        assert inputs == manifest["tracking_sha256"], "MATLAB/C tracking input hashes differ"
    solutions, observations = rows(args.actual / "solutions.csv"), rows(args.actual / "observations.csv")
    assert len(solutions) == run["epochs"] == run["valid_fixes"] == manifest["epochs"]
    assert all(r["valid"] == "1" and r["status"] == "ok" for r in solutions)
    assert all(r["used"] == "1" for r in observations)
    s_errors = compare_values(solutions, rows(args.reference / "solutions.csv"), SOLUTION_FIELDS)
    order = lambda r: (int(r["index"]), int(r["prn"]))
    o_errors = compare_values(sorted(observations, key=order),
                              sorted(rows(args.reference / "observations.csv"), key=order), OBSERVATION_FIELDS)
    for r in observations:
        raw, clock = float(r["raw_pseudorange_m"]), float(r["satellite_clock_s"])
        clock_corrected = raw + 299792458.0 * clock
        solution = solutions[int(r["index"])]
        assert abs(clock_corrected - float(r["clock_corrected_pseudorange_m"])) < 1e-7
        assert abs(clock_corrected - float(solution["clock_correction_m"]) -
                   float(r["receiver_corrected_pseudorange_m"])) < 1e-7
        assert int(r["absolute_sample"]) == int(solution["absolute_sample"])
    for r in solutions:
        assert abs(float(r["hdop"])**2 + float(r["vdop"])**2 - float(r["pdop"])**2) < 1e-10
    report = dict(passed=True, epochs=len(solutions), observations=len(observations),
                  solution_max_absolute_errors=s_errors, observation_max_absolute_errors=o_errors,
                  decoder_sha256=sha256(args.decoder), tracking_sha256=inputs,
                  reference_manifest_sha256=sha256(args.reference / "manifest.json"),
                  output_sha256={f.name: sha256(f) for f in args.actual.iterdir() if f.is_file()},
                  dop_note="HDOP/VDOP computed in ENU; original MATLAB ECEF-XY/Z values intentionally not used")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(solutions)} fixes / {len(observations)} observations; "
          f"max ECEF component error {max(s_errors[k] for k in ('x_m','y_m','z_m')):.3g} m")


if __name__ == "__main__":
    main()
