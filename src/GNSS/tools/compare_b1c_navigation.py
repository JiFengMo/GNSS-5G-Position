"""Compare C frames/fields against a fresh original MATLAB navigation run.

Run: python3 src/GNSS/tools/compare_b1c_navigation.py --help
Outputs a JSON report, exits nonzero on mismatched bits/timing/fields/inputs.
Next: use checked frame anchors for pseudorange work; see docs/navigation.md.
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
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def key(row):
    return tuple(int(row[k]) for k in ("prn", "generation", "first_epoch"))


def read_csv(path):
    with Path(path).open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def compare_fields(actual, reference):
    """Keep the documented IODC fix separate from the original field values."""
    eph = reference["ephemeris"]
    maximum = 0.0
    for name, value in eph.items():
        if name in ("flag", "PageID1", "PageID3"):
            continue
        if name == "SatType":
            value = {"GEO": 1, "IGSO": 2, "MEO": 3}[value]
        if name == "IODC":
            value = reference["IODC_corrected"]
        got = float(actual[name])
        if value == []:
            assert math.isnan(got), (name, got, "expected absent/NaN")
            continue
        assert math.isfinite(got), (name, got)
        # Decimal JSON round-trips and C %.17g normally agree exactly.
        assert math.isclose(got, value, rel_tol=2e-15, abs_tol=0), (name, got, value)
        maximum = max(maximum, abs(got - value))
    page = int(reference["bits"][614:620], 2)
    assert int(actual["PageID"]) == page
    assert int(actual["valid"]) == 1
    return maximum


def compare(reference_dir, actual_dir, tracking_dir):
    reference_dir, actual_dir, tracking_dir = map(Path, (reference_dir, actual_dir, tracking_dir))
    references = json.loads((reference_dir / "frames.json").read_text(encoding="utf-8"))
    if isinstance(references, dict):
        references = [references]
    manifest = json.loads((reference_dir / "manifest.json").read_text(encoding="utf-8"))
    run = json.loads((actual_dir / "run.json").read_text(encoding="utf-8"))
    assert run["complete"] and run["status_code"] == 0
    assert run["ldpc_error_correction"] is False
    assert run["secondary_max_errors"] == 0
    rows = read_csv(actual_dir / "frames.csv")
    frames = {key(r): r for r in rows if int(r["valid"])}
    eph_rows = read_csv(actual_dir / "ephemerides.csv")
    ephemerides = {key(r): r for r in eph_rows}
    assert len(frames) == sum(int(r["valid"]) for r in rows), "duplicate frame keys"
    assert len(ephemerides) == len(eph_rows), "duplicate ephemeris keys"
    expected = {key(r): r for r in references}
    assert len(expected) == len(references) > 0
    assert frames.keys() == expected.keys() == ephemerides.keys(), "frame sets differ"
    assert run["decoded_frames"] == len(frames)
    maximum = 0.0
    tracking = {}
    input_hashes = {}
    for prn in sorted({k[0] for k in expected}):
        name = f"prn_{prn:02d}"
        path = tracking_dir / f"{name}.csv"
        input_hashes[name] = sha256(path)
        if "tracking_sha256" in manifest:
            assert input_hashes[name] == manifest["tracking_sha256"][name], "tracking input hash mismatch"
        records = read_csv(path)
        tracking[prn] = {(int(r["generation"]), int(r["epoch"])): r for r in records}
    tow_by_prn = {}
    for k, ref in expected.items():
        frame, eph = frames[k], ephemerides[k]
        assert frame["status"] == "ok" and int(frame["crc2_ok"]) == int(frame["crc3_ok"]) == 1
        assert frame["bits"] == ref["bits"] and len(frame["bits"]) == 878
        for field in ("absolute_sample", "secondary_correlation", "inverted"):
            assert int(frame[field]) == ref[field], (k, field)
        assert int(eph["absolute_sample"]) == int(frame["absolute_sample"])
        assert float(frame["sample_time_s"]) == int(frame["absolute_sample"]) / run["sample_rate_hz"]
        assert float(eph["sample_time_s"]) == float(frame["sample_time_s"])
        maximum = max(maximum, compare_fields(eph, ref))
        # Independently verify all 1800 source epochs and the final sample.
        source = tracking[k[0]]
        previous_end = None
        for epoch in range(k[2], k[2] + 1800):
            r = source[k[1], epoch]
            assert int(r["valid"]) == 1 and r["state"] == "locked"
            start = int(r["absolute_sample"])
            assert previous_end is None or start == previous_end
            if epoch == k[2]:
                assert start == int(frame["absolute_sample"])
            previous_end = start + int(r["sample_count"])
        assert previous_end == int(frame["end_sample"])
        tow_by_prn.setdefault(k[0], []).append((k[1], k[2], int(eph["WN"]), float(eph["TOW"])))
    for values in tow_by_prn.values():
        values.sort()
        for a, b in zip(values, values[1:]):
            if a[0] == b[0]:
                assert (b[2] - a[2]) * 604800 + b[3] - a[3] == (b[1] - a[1]) * .01
    summary = read_csv(actual_dir / "summary.csv")
    assert sum(int(r["decoded_frames"]) for r in summary) == len(frames)
    return {
        "passed": True, "decoded_frames": len(frames), "prns": sorted(tow_by_prn),
        "bit_mismatches": 0, "frame_start_sample_max_error": 0,
        "ephemeris_field_max_absolute_error": maximum,
        "IODC_correction": "ICD 10-bit field, original MATLAB value // 2",
        "ldpc_error_correction": False, "tracking_sha256": input_hashes,
        "reference_manifest_sha256": sha256(reference_dir / "manifest.json"),
        "actual_outputs_sha256": {p.name: sha256(p) for p in actual_dir.iterdir() if p.is_file()},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--actual", type=Path, required=True)
    parser.add_argument("--tracking", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--decoder", type=Path, help="Record the tested C executable SHA-256")
    args = parser.parse_args()
    report = compare(args.reference, args.actual, args.tracking)
    if args.decoder:
        report["decoder_sha256"] = sha256(args.decoder)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {report['decoded_frames']} frames, {len(report['prns'])} PRNs; "
          f"878 bits/frame and all parsed fields match MATLAB (documented IODC fix)")


if __name__ == "__main__":
    main()
