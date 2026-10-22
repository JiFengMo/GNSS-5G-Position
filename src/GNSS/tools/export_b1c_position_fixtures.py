"""Export small deterministic regression fixtures from fresh MATLAB results.

Run with --reference MATLAB_POSITION_DIR --navigation MATLAB_NAV_DIR
--tracking C_TRACKING_DIR --output src/GNSS/tests/data.
Writes b1c_satellite_matlab.csv and b1c_position_matlab.json.gz; no IQ retained.
Next: run ctest --preset gnss-offline. See docs/positioning.md.
"""
import argparse
import csv
import gzip
import json
from pathlib import Path


def rows(path):
    with Path(path).open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("reference", "navigation", "tracking", "output"):
        p.add_argument(f"--{name}", type=Path, required=True)
    args = p.parse_args()
    frames = json.loads((args.navigation / "frames.json").read_text(encoding="utf-8"))
    first = {}
    for frame in frames:
        first.setdefault(frame["prn"], frame)
    args.output.mkdir(parents=True, exist_ok=True)
    vectors = rows(args.reference / "satellite_vectors.csv")
    with (args.output / "b1c_satellite_matlab.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, [*vectors[0], "bits"])
        writer.writeheader()
        for row in vectors:
            writer.writerow(dict(row, bits=first[int(row["prn"])]["bits"]))
    columns = ["epoch", "generation", "absolute_sample", "sample_count", "integration_s",
               "code_rate_hz", "rem_code_phase", "data_i_p", "pilot_q_p", "state", "valid"]
    tracking = {}
    for prn, frame in first.items():
        track = rows(args.tracking / f"prn_{prn:02d}.csv")
        start = next(j for j, r in enumerate(track) if int(r["epoch"]) == frame["first_epoch"]
                     and int(r["generation"]) == frame["generation"])
        selected = track[start:start + 1800]
        assert len(selected) == 1800
        tracking[str(prn)] = [[r[c] for c in columns] for r in selected]
    nav_columns = ["prn", "generation", "first_epoch", "absolute_sample", "end_sample",
                   "sample_time_s", "secondary_correlation", "inverted", "status", "crc2_ok", "crc3_ok", "valid", "bits"]
    nav_frames = []
    for prn, frame in first.items():
        r = dict(frame)
        final = tracking[str(prn)][-1]
        r.update(end_sample=int(final[2]) + int(final[3]), sample_time_s=frame["absolute_sample"] / 18e6,
                 status="ok", crc2_ok=1, crc3_ok=1, valid=1)
        for name in ("prn", "generation", "first_epoch", "absolute_sample", "end_sample",
                     "secondary_correlation", "inverted", "crc2_ok", "crc3_ok", "valid"):
            r[name] = int(r[name])
        nav_frames.append([r[c] for c in nav_columns])
    manifest = json.loads((args.reference / "manifest.json").read_text(encoding="utf-8"))
    start = max(f["absolute_sample"] for f in first.values()) + 1
    end = max(int(t[-1][2]) + int(t[-1][3]) for t in tracking.values())
    count = int((end - start) // (manifest["sample_rate_hz"] * manifest["period_ms"] / 1000))
    fixture = dict(schema_version=1, description="1800 actual tracked symbols/channel; first complete MATLAB frame, no IQ",
                   reference_manifest=manifest, tracking_columns=columns, tracking=tracking,
                   navigation_columns=nav_columns, navigation_frames=nav_frames,
                   solutions=rows(args.reference / "solutions.csv")[:count],
                   observations=[r for r in rows(args.reference / "observations.csv") if int(r["index"]) < count])
    compressed = gzip.compress(json.dumps(fixture, separators=(",", ":")).encode(), mtime=0)
    (args.output / "b1c_position_matlab.json.gz").write_bytes(compressed)
    print(f"Exported {len(vectors)} satellite vectors, {count} SPP epochs, {len(compressed)} fixture bytes")


if __name__ == "__main__":
    main()
