#!/usr/bin/env python3
"""Compare C against a newly exported MATLAB baseline, using the same IQ bytes.

Run from the project root; see ../docs/offline_acquisition.md for full commands.
Outputs: c_acquisition.csv, receiver.log, code_vectors.log, comparison.json.
Next: inspect comparison.json before using acquisition to initialize tracking.
Only the Python standard library is required.
"""

import argparse
from array import array
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time


def sha256(path):
    with path.open("rb") as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def rows(path):
    with path.open(encoding="utf-8", newline="") as stream:
        values = list(csv.DictReader(stream))
    result = {int(row["prn"]): row for row in values}
    if len(result) != len(values):
        raise ValueError(f"Duplicate PRNs in {path}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--receiver", type=Path, required=True)
    parser.add_argument("--code-test", type=Path, required=True)
    parser.add_argument("--iq", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path,
                        help="Optional CU-SDR root; verifies reference source hashes")
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((baseline / "manifest.json").read_text(encoding="utf-8"))
    if manifest["schema_version"] != 1 or manifest["input_format"] != "ci8":
        raise ValueError("Unsupported baseline schema or IQ format")
    with args.iq.open("rb") as stream:
        stream.seek(2 * manifest["first_sample_index"])
        raw = stream.read(2 * manifest["input_samples"])
    if (len(raw) != 2 * manifest["input_samples"] or
            hashlib.sha256(raw).hexdigest() != manifest["input_window_sha256"]):
        raise ValueError("IQ window does not match the MATLAB baseline SHA-256")
    if args.iq.stat().st_size != manifest["iq_file_bytes"]:
        raise ValueError("IQ file length differs from the baseline")
    if args.reference_root:
        for source in manifest["reference_files"]:
            relative = source["path"].replace("\\", "/").split("/BDS/B1C/")[-1]
            path = args.reference_root / "BDS" / "B1C" / relative
            if sha256(path) != source["sha256"]:
                raise ValueError(f"Reference source changed: {path}")
    fixtures = Path(__file__).resolve().parents[1] / "tests" / "data"
    code_command = [str(args.code_test.resolve()),
                    str(fixtures / "b1c_code_hashes.csv"),
                    str(fixtures / "b1c_sampled_hashes.csv"), str(baseline)]
    with (output / "code_vectors.log").open("w", encoding="utf-8") as log:
        subprocess.run(code_command, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=120)
    command = [str(args.receiver.resolve()), "--input", str(args.iq.resolve()),
               "--input-format", "ci8", "--sample-rate", str(manifest["sample_rate_hz"]),
               "--if", str(manifest["if_hz"]), "--prns", ",".join(map(str, manifest["prns"])),
               "--doppler-min", str(manifest["doppler_min_hz"]),
               "--doppler-max", str(manifest["doppler_max_hz"]),
               "--doppler-step", str(manifest["doppler_step_hz"]),
               "--fine-step", str(manifest["fine_step_hz"]),
               "--coherent-ms", str(manifest["coherent_integration_ms"]),
               "--threshold", str(manifest["threshold"]),
               "--skip-samples", str(manifest["first_sample_index"]),
               "--output", str(output / "c_acquisition.csv")]
    started = time.monotonic()
    print("Code vectors match MATLAB; running C acquisition on identical IQ...", flush=True)
    with (output / "receiver.log").open("w", encoding="utf-8") as log:
        completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=1800)
    if completed.returncode not in (0, 7):
        raise RuntimeError(f"C acquisition failed with {completed.returncode}; see receiver.log")
    elapsed = time.monotonic() - started
    expected = rows(baseline / "acquisition.csv")
    actual = rows(output / "c_acquisition.csv")
    failures, comparisons = [], []
    if set(expected) != set(actual) or set(expected) != set(manifest["prns"]):
        failures.append("PRN sets differ")
    for prn in sorted(set(expected) & set(actual)):
        a, b = expected[prn], actual[prn]
        reference_metric, c_metric = float(a["peak_metric"]), float(b["peak_metric"])
        reference_acquired, c_acquired = int(a["acquired"]), int(b["acquired"])
        relative_metric_error = abs(c_metric - reference_metric) / max(1.0, abs(reference_metric))
        detail = {"prn": prn, "reference_acquired": reference_acquired,
                  "c_acquired": c_acquired, "metric_relative_error": relative_metric_error}
        if not math.isfinite(c_metric) or relative_metric_error > 1e-7:
            failures.append(f"PRN {prn}: acquisition statistic differs")
        if c_acquired != reference_acquired:
            failures.append(f"PRN {prn}: detection differs")
        if reference_acquired and c_acquired:
            phase_error = int(b["code_phase_samples"]) - int(a["code_phase_samples"])
            frequency_error = float(b["carrier_hz"]) - float(a["carrier_hz"])
            detail.update(phase_error_samples=phase_error, frequency_error_hz=frequency_error,
                          carrier_hz=float(b["carrier_hz"]), doppler_hz=float(b["doppler_hz"]),
                          code_phase_samples=int(b["code_phase_samples"]), peak_metric=c_metric)
            if phase_error != 0 or frequency_error != 0:
                failures.append(f"PRN {prn}: frequency/code phase differs")
        comparisons.append(detail)
    # Check the legacy cf32 reader against nonzero real IQ as well as ci8.
    acquired_prns = [prn for prn, row in actual.items() if int(row["acquired"])]
    format_prn = acquired_prns[0] if acquired_prns else manifest["prns"][0]
    floats = array("f", array("b", raw))
    if sys.byteorder != "little":
        floats.byteswap()
    cf32_path = output / "window.cf32"
    cf32_path.write_bytes(floats.tobytes())
    format_command = command.copy()
    for option, value in [("--input", str(cf32_path)), ("--input-format", "cf32"),
                          ("--skip-samples", "0"), ("--prns", str(format_prn)),
                          ("--output", str(output / "cf32_acquisition.csv"))]:
        format_command[format_command.index(option) + 1] = value
    with (output / "cf32_receiver.log").open("w", encoding="utf-8") as log:
        format_run = subprocess.run(format_command, stdout=log, stderr=subprocess.STDOUT, timeout=300)
    if format_run.returncode not in (0, 7):
        raise RuntimeError("cf32 validation failed; see cf32_receiver.log")
    cf32_row = rows(output / "cf32_acquisition.csv")[format_prn]
    ci8_row = actual[format_prn]
    format_passed = (cf32_row["acquired"] == ci8_row["acquired"] and
                     cf32_row["code_phase_samples"] == ci8_row["code_phase_samples"] and
                     math.isclose(float(cf32_row["peak_metric"]), float(ci8_row["peak_metric"]), rel_tol=1e-12))
    if int(ci8_row["acquired"]):
        format_passed &= float(cf32_row["carrier_hz"]) == float(ci8_row["carrier_hz"])
    if not format_passed:
        failures.append("ci8 and cf32 readers disagree on identical real IQ")
    report = {"passed": not failures, "failures": failures,
              "input_window_sha256": manifest["input_window_sha256"],
              "receiver_sha256": sha256(args.receiver),
              "matlab_acquisition_elapsed_s": manifest["acquisition_elapsed_s"],
              "c_acquisition_elapsed_s": elapsed,
              "acquired_prns": acquired_prns,
              "cf32_equivalence_prn": format_prn, "cf32_equivalence_passed": format_passed,
              "tolerances": {"phase_samples": 0, "frequency_hz": 0, "relative_metric": 1e-7},
              "results": comparisons}
    (output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "results"}, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
