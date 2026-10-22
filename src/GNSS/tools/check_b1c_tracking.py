#!/usr/bin/env python3
"""Check tracking time, generations, quality flags and reacquisition events.

Run: python3 src/GNSS/tools/check_b1c_tracking.py OUTPUT_DIR --report REPORT_JSON
Outputs a JSON validation/quality summary. Next: inspect channels without valid
epochs before using the stream in future navigation-message processing.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import statistics


def read_csv(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    metadata = json.loads((args.directory / "run.json").read_text(encoding="utf-8"))
    fs = metadata["sample_rate_hz"]
    events = read_csv(args.directory / "events.csv")
    summaries = read_csv(args.directory / "summary.csv")
    failures, results = [], []
    expected_files = {f"prn_{int(row['prn']):02d}.csv" for row in summaries}
    if {path.name for path in args.directory.glob("prn_*.csv")} != expected_files:
        failures.append("PRN files differ from this run's channel summary")
    if not metadata["complete"] or metadata["status_code"] != 0:
        failures.append("Requested tracking run did not complete")
    for summary in summaries:
        prn = int(summary["prn"])
        records = read_csv(args.directory / f"prn_{prn:02d}.csv")
        if len(records) != metadata["requested_epochs_per_channel"]:
            failures.append(f"PRN {prn}: wrong epoch count")
        if not records:
            continue
        previous, lost_seen, valid = None, False, []
        generations = set()
        reacquisitions = {(int(e["generation"]), int(e["absolute_sample"])) for e in events
                          if int(e["prn"]) == prn and e["event"] == "reacquire_ok"}
        for n, row in enumerate(records):
            start, size = int(row["absolute_sample"]), int(row["sample_count"])
            generation = int(row["generation"])
            if n == 0 and (generation != 0 or int(row["epoch"]) != 0):
                failures.append(f"PRN {prn}: missing initial generation")
            generations.add(generation)
            if int(row["record_index"]) != n or size <= 0:
                failures.append(f"PRN {prn}: record index/length")
            if abs(float(row["sample_time_s"]) - start / fs) > 1e-12:
                failures.append(f"PRN {prn}: incorrect sample time")
            if abs(float(row["integration_s"]) - size / fs) > 1e-12:
                failures.append(f"PRN {prn}: incorrect integration duration")
            if previous:
                old_start = int(previous["absolute_sample"])
                old_size = int(previous["sample_count"])
                if generation == int(previous["generation"]):
                    if start != old_start + old_size or int(row["epoch"]) != int(previous["epoch"]) + 1:
                        failures.append(f"PRN {prn}: sample/epoch gap inside generation")
                    cycle_error = float(row["carrier_cycles"]) - float(previous["carrier_cycles"]) - float(previous["carrier_hz"]) * old_size / fs
                    code_error = float(row["rem_code_phase"]) - (float(previous["rem_code_phase"]) + old_size * float(previous["code_rate_hz"]) / fs - 10230)
                    phase_error = math.remainder(float(row["rem_carrier_phase"]) - float(previous["rem_carrier_phase"]) - 2 * math.pi * float(previous["carrier_hz"]) * old_size / fs, 2 * math.pi)
                    if max(abs(cycle_error), abs(code_error), abs(phase_error)) > 1e-7:
                        failures.append(f"PRN {prn}: NCO phase discontinuity")
                else:
                    lost_seen = False
                    if (generation != int(previous["generation"]) + 1 or int(row["epoch"]) != 0 or
                            start < old_start + old_size or (generation, start) not in reacquisitions or
                            float(row["carrier_cycles"]) != 0 or row["valid"] != "0" or row["state"] != "pull_in"):
                        failures.append(f"PRN {prn}: invalid reacquisition boundary")
            lost_seen |= row["state"] == "lost"
            if row["valid"] == "1":
                residual = float(row["residual_frequency_hz"])
                cn0 = float(row["cn0_db_hz"])
                lock = float(row["lock_indicator"])
                if (lost_seen or row["state"] != "locked" or row["quality_good"] != "1" or
                        math.isnan(cn0) or cn0 < metadata["min_cn0_db_hz"] or
                        not math.isfinite(lock) or lock < metadata["min_lock_indicator"] or
                        not math.isfinite(residual) or
                        abs(residual) >= 1.6 * metadata["max_residual_frequency_hz"]):
                    failures.append(f"PRN {prn}: invalid quality flag")
                valid.append(row)
            previous = row
        if len(valid) != int(summary["valid_epochs"]):
            failures.append(f"PRN {prn}: summary validity count differs")
        if len(reacquisitions) != int(summary["reacquisition_successes"]):
            failures.append(f"PRN {prn}: summary reacquisition count differs")
        finite_cn0 = [float(x["cn0_db_hz"]) for x in valid
                      if x["quality_updated"] == "1" and math.isfinite(float(x["cn0_db_hz"]))]
        results.append({"prn": prn, "epochs": len(records), "valid_epochs": len(valid),
                        "generations": len(generations), "reacquisitions": len(reacquisitions),
                        "final_state": records[-1]["state"],
                        "first_valid_time_s": float(valid[0]["sample_time_s"]) if valid else None,
                        "median_valid_cn0_db_hz": statistics.median(finite_cn0) if finite_cn0 else None})
    report = {"passed": not failures, "failures": sorted(set(failures)), "results": results,
              "note": "A passed consistency check does not assert navigation validity or that all channels locked."}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
