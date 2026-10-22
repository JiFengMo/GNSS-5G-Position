#!/usr/bin/env python3
"""Verify completed tracking CSVs against a fresh MATLAB narrowband baseline.

Run: python3 src/GNSS/tools/compare_b1c_tracking.py --reference REF_DIR
     --actual C_DIR --iq IQ_FILE --output REPORT_JSON [--tracker EXECUTABLE]
Outputs: a JSON report of all epochs, timing, correlators, loop errors and lock quality.
Next: review per-channel quality separately from agreement with the reference loops.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import time


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def rows(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--actual", type=Path, required=True)
    parser.add_argument("--iq", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tracker", type=Path)
    parser.add_argument("--run", action="store_true", help="Run C tracking before comparing")
    parser.add_argument("--acquisition-csv", type=Path)
    args = parser.parse_args()
    reference = json.loads((args.reference / "manifest.json").read_text(encoding="utf-8"))
    if args.run:
        if not args.tracker or not args.acquisition_csv:
            parser.error("--run needs --tracker and --acquisition-csv")
        args.actual.mkdir(parents=True, exist_ok=True)
        command = [str(args.tracker.resolve()), "--input", str(args.iq.resolve()),
                   "--input-format", reference["input_format"],
                   "--sample-rate", str(reference["sample_rate_hz"]), "--if", str(reference["if_hz"]),
                   "--acquisition-csv", str(args.acquisition_csv.resolve()),
                   "--seconds", str(reference["duration_seconds"]), "--no-reacquire",
                   "--output-dir", str(args.actual.resolve())]
        print("Running continuous C tracking on all acquired channels...", flush=True)
        started = time.monotonic()
        with (args.actual / "tracker.log").open("w", encoding="utf-8") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=3600)
        run_path = args.actual / "run.json"
        run_metadata = json.loads(run_path.read_text(encoding="utf-8"))
        run_metadata["elapsed_s"] = time.monotonic() - started
        run_path.write_text(json.dumps(run_metadata, indent=2) + "\n", encoding="utf-8")
    actual = json.loads((args.actual / "run.json").read_text(encoding="utf-8"))
    failures, results = [], []
    if not actual["complete"] or actual["status_code"] != 0 or actual["max_reacquisitions"] != 0:
        raise ValueError("Full trajectory comparison requires a completed --no-reacquire run")
    for key in ("sample_rate_hz", "if_hz", "dll_bandwidth_hz", "dll_damping",
                "early_late_offset_chips", "pll_bandwidth_hz", "quality_epochs"):
        if reference[key] != actual[key]:
            raise ValueError(f"Different tracking settings: {key}")
    if actual["requested_seconds"] != reference["duration_seconds"]:
        raise ValueError("Duration differs")
    if args.iq.stat().st_size != reference["iq_file_bytes"]:
        raise ValueError("IQ length differs")
    iq_hash = sha256(args.iq)
    if iq_hash != reference.get("iq_sha256"):
        raise ValueError("Full IQ SHA-256 differs or is missing from the baseline manifest")
    fs = reference["sample_rate_hz"]
    expected_count = round(reference["duration_seconds"] / reference["integration_s"])
    # Time-adjusted phase accounts for a rare one-sample ceil boundary.
    limits = {"absolute_sample": 1, "carrier_hz": .05, "code_rate_hz": .01,
              "dll_error": .005, "pll_error": .001,
              "adjusted_code_phase_chips": .001,
              "adjusted_carrier_phase_rad": .02, "correlator_normalized_error": .02}
    for prn in sorted(reference["prns"]):
        r = rows(args.reference / f"prn_{prn:02d}.csv")
        c = rows(args.actual / f"prn_{prn:02d}.csv")
        if len(r) != expected_count or len(c) != expected_count:
            failures.append(f"PRN {prn}: incomplete epoch count")
            continue
        errors = {key: 0.0 for key in limits}
        scales = {}
        for i, q in [("data_i_e", "data_q_e"), ("data_i_p", "data_q_p"),
                     ("data_i_l", "data_q_l"), ("pilot_i_p", "pilot_q_p")]:
            scales[i] = max(1.0, max(math.hypot(float(x[i]), float(x[q])) for x in r))
        previous_end, lost_seen, valid, locked = None, False, 0, 0
        quality_cn0, residuals = [], []
        for n, (a, b) in enumerate(zip(r, c)):
            if int(a["epoch"]) != n or int(b["epoch"]) != n or int(b["generation"]) != 0:
                failures.append(f"PRN {prn}: unexpected epoch/generation")
                break
            start, size = int(b["absolute_sample"]), int(b["sample_count"])
            if previous_end is not None and start != previous_end:
                failures.append(f"PRN {prn}: discontinuous input samples")
                break
            previous_end = start + size
            if abs(float(b["sample_time_s"]) - start / fs) > 1e-12:
                failures.append(f"PRN {prn}: inconsistent record time")
                break
            if abs(float(b["integration_s"]) - size / fs) > 1e-12:
                failures.append(f"PRN {prn}: inconsistent integration duration")
                break
            for key in ("absolute_sample", "carrier_hz", "code_rate_hz", "dll_error", "pll_error"):
                difference = abs(float(a[key]) - float(b[key]))
                if not math.isfinite(difference):
                    failures.append(f"PRN {prn}: nonfinite loop output")
                    break
                errors[key] = max(errors[key], difference)
            offset = int(a["absolute_sample"]) - start
            code_error = float(b["rem_code_phase"]) + offset * float(b["code_rate_hz"]) / fs - float(a["rem_code_phase"])
            errors["adjusted_code_phase_chips"] = max(errors["adjusted_code_phase_chips"], abs(code_error))
            carrier_error = (float(b["rem_carrier_phase"]) + offset * float(b["carrier_hz"]) * 2 * math.pi / fs - float(a["rem_carrier_phase"]))
            carrier_error = math.remainder(carrier_error, 2 * math.pi)
            errors["adjusted_carrier_phase_rad"] = max(errors["adjusted_carrier_phase_rad"], abs(carrier_error))
            for i, q in [("data_i_e", "data_q_e"), ("data_i_p", "data_q_p"),
                         ("data_i_l", "data_q_l"), ("pilot_i_p", "pilot_q_p")]:
                difference = math.hypot(float(a[i]) - float(b[i]), float(a[q]) - float(b[q])) / scales[i]
                errors["correlator_normalized_error"] = max(errors["correlator_normalized_error"], difference)
                if n == 0 and difference > 1e-9:
                    failures.append(f"PRN {prn}: first-epoch correlators disagree")
            lost_seen |= b["state"] == "lost"
            is_valid = int(b["valid"]) == 1
            if is_valid and (lost_seen or b["state"] != "locked" or b["quality_good"] != "1"):
                failures.append(f"PRN {prn}: invalid lock-state transition")
            valid += is_valid
            locked += b["state"] == "locked"
            if b["quality_updated"] == "1" and math.isfinite(float(b["cn0_db_hz"])):
                quality_cn0.append(float(b["cn0_db_hz"]))
            if is_valid:
                residuals.append(abs(float(b["residual_frequency_hz"])))
        for key, maximum in errors.items():
            if maximum > limits[key]:
                failures.append(f"PRN {prn}: {key} exceeds tolerance ({maximum})")
        results.append({"prn": prn, "epochs": len(c), "valid_epochs": valid,
                        "locked_epochs": locked, "final_state": c[-1]["state"],
                        "median_cn0_db_hz": statistics.median(quality_cn0) if quality_cn0 else None,
                        "median_abs_residual_frequency_hz": statistics.median(residuals) if residuals else None,
                        "max_errors": errors})
    report = {"passed": not failures, "failures": failures, "iq_sha256": iq_hash,
              "reference_elapsed_s": reference["elapsed_s"], "tolerances": limits,
              "c_elapsed_s": actual.get("elapsed_s"),
              "results": results,
              "note": "Loop agreement and tracking-quality validity are separate checks; no navigation validity is claimed."}
    if args.tracker:
        report["tracker_sha256"] = sha256(args.tracker)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
