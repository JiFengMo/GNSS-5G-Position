#!/usr/bin/env python3
"""Import an offline B1C experiment for the existing browser UI (stdlib only).

Example from project root:
python3 ui/import_gnss.py --tracking-dir log/gnss/tracking-01
  --navigation-dir log/gnss/navigation-01 --position-dir log/gnss/position-01
  --acquisition-csv log/gnss/acquisition.csv --output-dir log/gnss-ui/experiment-01
Outputs data.sqlite + run.json in a NEW directory. Next: run
python3 ui/server.py --config config/config.f, choose 北斗 B1C in the browser.
"""
import argparse
import sqlite3
from pathlib import Path
from gnss import import_run


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--tracking-dir",type=Path,required=True)
    p.add_argument("--navigation-dir",type=Path)
    p.add_argument("--position-dir",type=Path)
    p.add_argument("--acquisition-csv",type=Path)
    p.add_argument("--output-dir",type=Path,required=True)
    p.add_argument("--name",help="Display name of this experiment")
    a=p.parse_args()
    try:
        m=import_run(a.tracking_dir,a.output_dir,navigation=a.navigation_dir,positioning=a.position_dir,
                     acquisition=a.acquisition_csv,name=a.name)
    except (OSError,ValueError,KeyError,OverflowError,sqlite3.Error) as exc:
        p.exit(2,f"GNSS import failed: {exc}\n")
    print(f"Imported {len(m['prns'])} channels / {m['valid_frames']} valid frames / {m['valid_fixes']} fixes -> {a.output_dir}")
    print("Next: python3 ui/server.py --config config/config.f; select 北斗 B1C / 离线结果回看")


if __name__=="__main__": main()
