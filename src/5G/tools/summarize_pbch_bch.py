#!/usr/bin/env python3
"""Summarize nr-toa-uesoftmodem PBCH decode lines from logs or stdin."""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from collections import Counter
from dataclasses import asdict, dataclass
from typing import Iterable


PBCH_OK_RE = re.compile(
    r"pbch_bch:\s+ok\s+"
    r"pci=(?P<pci>\d+)\s+"
    r"ssb=(?P<ssb>\d+)\s+"
    r"nhf=(?P<nhf>\d+)\s+"
    r"dmrs=(?P<dmrs>[-+]?\d+(?:\.\d+)?)\s+"
    r"dt=(?P<dt>-?\d+)\s+"
    r"dcfo=(?P<dcfo>[-+]?\d+(?:\.\d+)?)\s+"
    r"pss=(?P<pss>[-+]?\d+(?:\.\d+)?)\s+"
    r"snr=(?P<snr>[-+]?\d+(?:\.\d+)?)\s+"
    r"sfn=(?P<sfn>\d+)\s+"
    r"mib=0x(?P<mib>[0-9a-fA-F]+)"
)

KEYVAL_RE = re.compile(r"(\w+)=([^\s]+)")


@dataclass(frozen=True)
class PbchOk:
    line_no: int
    pci: int
    ssb: int
    nhf: int
    dmrs: float
    dt: int
    dcfo: float
    pss: float
    snr: float
    sfn: int
    mib: str


def read_lines(paths: list[str]) -> Iterable[tuple[str, int, str]]:
    if not paths or paths == ["-"]:
        for line_no, line in enumerate(sys.stdin, 1):
            yield "stdin", line_no, line.rstrip("\n")
        return

    for path in paths:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for line_no, line in enumerate(handle, 1):
                yield path, line_no, line.rstrip("\n")


def parse_pbch_ok(line_no: int, line: str) -> PbchOk | None:
    match = PBCH_OK_RE.search(line)
    if not match:
        return None
    fields = match.groupdict()
    return PbchOk(
        line_no=line_no,
        pci=int(fields["pci"]),
        ssb=int(fields["ssb"]),
        nhf=int(fields["nhf"]),
        dmrs=float(fields["dmrs"]),
        dt=int(fields["dt"]),
        dcfo=float(fields["dcfo"]),
        pss=float(fields["pss"]),
        snr=float(fields["snr"]),
        sfn=int(fields["sfn"]),
        mib=f"0x{fields['mib'].lower()}",
    )


def stats(values: list[float]) -> dict[str, float] | None:
    if not values:
        return None
    return {
        "min": min(values),
        "avg": statistics.fmean(values),
        "max": max(values),
    }


def counter_to_dict(counter: Counter[object]) -> dict[str, int]:
    return {str(key): value for key, value in counter.most_common()}


def format_stats(label: str, values: list[float], unit: str = "") -> str:
    s = stats(values)
    if s is None:
        return f"{label}: n/a"
    suffix = f" {unit}" if unit else ""
    return (
        f"{label}: min={s['min']:.3f}{suffix} "
        f"avg={s['avg']:.3f}{suffix} max={s['max']:.3f}{suffix}"
    )


def summarize(paths: list[str]) -> dict[str, object]:
    pbch_ok: list[PbchOk] = []
    crc_fail = 0
    lock_events = 0
    sync_results = 0
    confirmed_sync_results = 0
    sources: Counter[str] = Counter()

    for source, line_no, line in read_lines(paths):
        sources[source] += 1
        ok = parse_pbch_ok(line_no, line)
        if ok is not None:
            pbch_ok.append(ok)
        if "pbch_bch: crc-fail" in line:
            crc_fail += 1
        if "SSB_LOCK_EVENT:" in line:
            lock_events += 1
        if "sync_result:" in line:
            sync_results += 1
            values = dict(KEYVAL_RE.findall(line))
            if values.get("pbch_confirmed") == "1" and values.get("mib") == "1":
                confirmed_sync_results += 1

    pci_ssb = Counter(f"pci={ok.pci},ssb={ok.ssb}" for ok in pbch_ok)
    mibs = Counter(ok.mib for ok in pbch_ok)
    sfns = [ok.sfn for ok in pbch_ok]
    summary = {
        "sources": dict(sources),
        "pbch_ok_count": len(pbch_ok),
        "pbch_crc_fail_count": crc_fail,
        "ssb_lock_event_count": lock_events,
        "sync_result_count": sync_results,
        "confirmed_sync_result_count": confirmed_sync_results,
        "pci_ssb": counter_to_dict(pci_ssb),
        "mib_payloads": counter_to_dict(mibs),
        "sfn_first": sfns[0] if sfns else None,
        "sfn_last": sfns[-1] if sfns else None,
        "dmrs": stats([ok.dmrs for ok in pbch_ok]),
        "snr_db": stats([ok.snr for ok in pbch_ok]),
        "pss_metric": stats([ok.pss for ok in pbch_ok]),
        "dcfo_hz": stats([ok.dcfo for ok in pbch_ok]),
        "records": [asdict(ok) for ok in pbch_ok],
    }
    return summary


def print_text(summary: dict[str, object], show_records: bool) -> None:
    print("PBCH BCH summary")
    print(f"sources: {', '.join(summary['sources']) or 'stdin'}")
    print(f"pbch_bch ok: {summary['pbch_ok_count']}")
    print(f"pbch_bch crc-fail: {summary['pbch_crc_fail_count']}")
    print(f"SSB_LOCK_EVENT: {summary['ssb_lock_event_count']}")
    print(
        "sync_result confirmed: "
        f"{summary['confirmed_sync_result_count']}/{summary['sync_result_count']}"
    )

    if summary["pbch_ok_count"] == 0:
        print("result: no PBCH OK line found")
        return

    print(f"PCI/SSB: {summary['pci_ssb']}")
    print(f"MIB payloads: {summary['mib_payloads']}")
    print(f"SFN: first={summary['sfn_first']} last={summary['sfn_last']}")
    records = summary["records"]
    assert isinstance(records, list)
    print(format_stats("DMRS", [record["dmrs"] for record in records]))
    print(format_stats("SNR", [record["snr"] for record in records], "dB"))
    print(format_stats("PSS metric", [record["pss"] for record in records]))
    print(format_stats("delta CFO", [record["dcfo"] for record in records], "Hz"))

    if show_records:
        print()
        print("PBCH OK records:")
        for record in records:
            print(
                f"  line={record['line_no']} pci={record['pci']} ssb={record['ssb']} "
                f"nhf={record['nhf']} sfn={record['sfn']} mib={record['mib']} "
                f"dmrs={record['dmrs']:.3f} snr={record['snr']:.2f} "
                f"pss={record['pss']:.3f} dcfo={record['dcfo']:.1f}"
            )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Summarize pbch_bch: ok lines from nr-toa-uesoftmodem logs."
    )
    parser.add_argument(
        "logfile",
        nargs="*",
        help="Log file(s) to parse. Omit or pass '-' to read stdin.",
    )
    parser.add_argument(
        "--records",
        action="store_true",
        help="Print every PBCH OK record after the summary.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON instead of text.",
    )
    args = parser.parse_args()

    summary = summarize(args.logfile)
    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print_text(summary, args.records)
    return 0 if summary["pbch_ok_count"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
