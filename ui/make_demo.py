#!/usr/bin/env python3
"""Create explicitly labelled synthetic records for UI/replay checks, never live SDR data."""
import argparse
import json
import math
from pathlib import Path
import time
from server import load_settings, ROOT


def generate(directory: Path, count=180) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    started = int(time.time() * 1000) - count * 200
    run = f"demo-{started}"
    path = directory / f"{run}-000.jsonl"
    config = dict(schema=1, kind="config", run_id=run, time_ms=started, demo=True,
                  source_mode="controlled", time_model="shared", absolute_toa_calibrated=True,
                  configured=dict(center_freq_hz=3600000000, sample_rate_hz=30720000, rx_gain_db=30,
                                  target_pci=-1, target_ssb_index=-1, scan_target_count=2,
                                  clock_source="external", time_source="external", sdr="usrp", provider="ssb"),
                  position=dict(valid=False, reason="not_implemented", xyz_m=None))
    with path.open("w", encoding="utf-8", newline="\n") as fp:
        fp.write(json.dumps(config) + "\n")
        seq = 0
        for j in range(count):
            seq += 1; t = started + j * 200
            runtime = dict(schema=1,kind="runtime",run_id=run,seq=seq,time_ms=t,state=3,clock_locked=True,
                           ref_locked=True,pps_locked=True,gps_locked=False,sample_rate_hz=30720000,
                           capture_freq_hz=3600000000,rx_reads=j*12,rx_gaps=0,sync_dropped=0,ring_overruns=0,dropped=0,io_errors=0)
            fp.write(json.dumps(runtime) + "\n")
            for station in range(2):
                seq += 1; locked = not (station == 1 and 55 <= j < 75)
                curves = {}
                if j % 5 == 0:
                    x = list(range(256))
                    def curve(y):
                        return dict(original_points=256, x=x, y=y)
                    curves["iq_i"] = curve([1200*math.sin(i*.21+j*.05) for i in x])
                    curves["iq_q"] = curve([1000*math.cos(i*.21+j*.05) for i in x])
                    for nid in range(3):
                        curves[f"pss_{nid}"] = curve([.5+.15*math.sin(i*.9+nid)**2+(8 if nid == station else 1.5)*math.exp(-((i-120-station*8)/5)**2) for i in x])
                    if locked:
                        curves["channel_magnitude"] = curve([1+.3*math.sin(i*.035)+.12*math.cos(i*.13) for i in x])
                        curves["channel_phase"] = curve([math.sin(i*.024) for i in x])
                        curves["cir_magnitude"] = curve([.02+2*math.exp(-((i-32)/2.2)**2)+.6*math.exp(-((i-61)/3)**2) for i in x])
                        curves["fractional_peak"] = dict(original_points=3,x=[31,32,33],y=[1.7,2.1,1.85])
                event = dict(schema=1,kind="sample",run_id=run,seq=seq,time_ms=t+station,event_id=f"{run}:{j}:{station}",
                             hardware_sample=str(10000000000000000+j*6144000),abs_sample=str(j*6144000),
                             freq_hz=3600000000+station*10000000,sample_rate_hz=30720000,nsamps=650000,
                             pci=84+station,ssb=station,locked=locked,mib_ok=locked,pbch_ok=locked,fail_stage=0 if locked else 5,
                             coarse_offset_samp=120+station*8,tracking_shift_samp=j%3,pss_step_samp=4,
                             cfo_hz=125+station*90+4*math.sin(j*.1),snr_db=23+station*2,pss_metric=12+station,
                             relative_delay_ns=(125+station*30+math.sin(j*.15)) if locked else None,
                             absolute_candidate_valid=locked,toa_candidate_ns=85+station*20+math.sin(j*.1),
                             clock_locked=True,quality=.9 if locked else 0,cir_peak_sample=32 if locked else None,
                             fractional_offset_samp=.12 if locked else None,curves=curves,dropped=0,io_errors=0)
                fp.write(json.dumps(event, separators=(",", ":"),allow_nan=False)+"\n")
    return path


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config",type=Path,default=ROOT/"config/config.f")
    args=parser.parse_args()
    print(generate(load_settings(args.config)["directory"]))
    print("Synthetic demo only. Select it under 记录回看; it is excluded from live monitoring.")
