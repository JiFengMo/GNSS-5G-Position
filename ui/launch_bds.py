#!/usr/bin/env python3
"""B1C offline launcher. Run: sh run_BDS_only.sh [--ui-only|--check].

Reads config/GNSS/offline.f, incrementally builds gnss-offline, runs acquisition,
tracking, navigation, SPP and UI import. Outputs new log/gnss/runs/<id>/ and
log/gnss-ui/<id>/ plus log/launcher/<id>/. Next: open the printed UI address.
Ctrl+C stops only the current child; --ui-only opens existing results directly.
"""
from __future__ import annotations
import argparse
from collections import deque
from datetime import datetime
import math
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys

from gnss import import_run
from server import ROOT,load_settings,read_config

JOB_KEYS={"input_file","input_format","sample_rate_hz","if_hz","seconds","prns","ui_port"}


def job_settings(path,process=True):
    job=read_config(path)
    if set(job)!=JOB_KEYS:
        raise ValueError(f"Offline config requires {sorted(JOB_KEYS)}; unknown/missing: {sorted(set(job)^JOB_KEYS)}")
    port=int(job["ui_port"])
    if not 1024<=port<=65535:raise ValueError("ui_port must be 1024..65535")
    job["ui_port"]=port
    if not process:return job
    for key in ("sample_rate_hz","if_hz","seconds"):
        job[key]=float(job[key])
        if not math.isfinite(job[key]):raise ValueError(f"Invalid {key}")
    fs=job["sample_rate_hz"];seconds=job["seconds"]
    if not 2046000<=fs<=1e8 or abs(fs*.01-round(fs*.01))>1e-7:
        raise ValueError("sample_rate_hz must be 2.046..100 MHz with an integer 10 ms sample count")
    if not .01<=seconds<=3600 or abs(seconds*100-round(seconds*100))>1e-7:
        raise ValueError("seconds must be a multiple of .01 in .01..3600")
    if job["input_format"] not in ("ci8","cf32"):raise ValueError("input_format must be ci8 or cf32")
    prns=[]
    for term in job["prns"].split(","):
        if not re.fullmatch(r"[0-9]+(?:-[0-9]+)?",term):raise ValueError("Invalid prns list")
        first,last=term.split("-") if "-" in term else (term,term)
        first,last=int(first),int(last)
        if not 1<=first<=last<=63:raise ValueError("PRNs must be 1..63")
        prns.extend(range(first,last+1))
    if len(set(prns))!=len(prns):raise ValueError("Duplicate PRNs")
    source=(ROOT/Path(job["input_file"])).resolve()
    if not source.is_file():raise ValueError(f"IQ file is unavailable: {source}; edit {path}")
    width=2 if job["input_format"]=="ci8" else 8
    if source.stat().st_size<round(fs*max(.02,seconds))*width:
        raise ValueError("IQ file is shorter than the requested nominal duration")
    job["input_file"]=source
    return job


def run_child(command,log_path=None):
    """No shell evaluation; signals terminate only this invocation's child."""
    output=log_path.open("w",encoding="utf-8") if log_path else None
    child=None
    try:
        child=subprocess.Popen([str(v) for v in command],cwd=ROOT,stdout=output,stderr=subprocess.STDOUT if output else None)
        try:
            status=child.wait()
        except BaseException:
            child.terminate()
            try:child.wait(timeout=10)
            except subprocess.TimeoutExpired:child.kill();child.wait()
            raise
    finally:
        if output:output.close()
    if status:
        if log_path:
            with log_path.open(encoding="utf-8",errors="replace") as source:
                print("".join(deque(source,maxlen=20)),file=sys.stderr)
        raise subprocess.CalledProcessError(status,command)


def process_job(job,settings,identifier,run=run_child,importer=import_run):
    """One fresh result folder per run; never reuse another capture's CSV."""
    logs=ROOT/"log/launcher"/identifier;logs.mkdir(parents=True,exist_ok=False)
    results=ROOT/"log/gnss/runs"/identifier;results.mkdir(parents=True,exist_ok=False)
    print(f"北斗离线处理；阶段日志：{logs}\n结果目录：{results}",flush=True)
    run(["cmake","--preset","gnss-offline"],logs/"configure.log")
    run(["cmake","--build","--preset","gnss-offline","--parallel","2"],logs/"build.log")
    binaries=ROOT/"build/gnss-offline/src/GNSS"
    capture=results/"acquisition.csv";track=results/"tracking";nav=results/"navigation";position=results/"position"
    fs=job["sample_rate_hz"]
    common=["--input",job["input_file"],"--input-format",job["input_format"],"--sample-rate",fs,"--if",job["if_hz"]]
    stages=[
        ("捕获",[binaries/"bds-b1c-receiver",*common,"--prns",job["prns"],"--output",capture],"acquisition"),
        ("连续跟踪",[binaries/"bds-b1c-track",*common,"--acquisition-csv",capture,"--seconds",job["seconds"],"--output-dir",track],"tracking"),
        ("导航电文",[binaries/"bds-b1c-nav","--tracking-dir",track,"--sample-rate",fs,"--output-dir",nav],"navigation"),
        ("单点定位",[binaries/"bds-b1c-position","--tracking-dir",track,"--navigation-dir",nav,"--sample-rate",fs,"--output-dir",position],"position")]
    for label,command,name in stages:
        print(f"正在进行：{label}；日志 {logs/(name+'.log')}",flush=True)
        run(command,logs/(name+".log"))
    imported=settings["gnss_directory"]/identifier
    meta=importer(track,imported,navigation=nav,positioning=position,acquisition=capture,name=f"B1C {identifier}")
    print(f"处理与导入完成：{len(meta['prns'])} 通道 / {meta['valid_frames']} 帧 / {meta['valid_fixes']} 有效定位解",flush=True)
    return imported


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument("--ui-only",action="store_true",help="Only display imported BDS results; no build or IQ processing")
    mode.add_argument("--check",action="store_true",help="Validate job/UI configuration and input file, without processing")
    parser.add_argument("--config",type=Path,default=ROOT/"config/GNSS/offline.f",help="BDS offline job configuration")
    parser.add_argument("--main-config",type=Path,default=ROOT/"config/config.f",help="Shared UI configuration entry")
    args=parser.parse_args()
    def terminate(_signum,_frame):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,terminate)
    try:
        settings=load_settings(args.main_config)
        job=job_settings(args.config,process=not args.ui_only)
        port=job["ui_port"]
        if args.check:
            print(f"北斗配置检查通过；IQ：{job['input_file']}；网页端口：{port}")
            return 0
        # Do not briefly bind and release the UI port: under WSL mirrored
        # networking that can leave a Windows forwarding reservation. Detect
        # an existing listener here; the HTTP server performs the final bind.
        with socket.socket() as probe:
            probe.settimeout(1)
            if probe.connect_ex(("127.0.0.1",port))==0:
                raise ValueError(f"北斗 UI 端口 {port} 已有服务；请修改 {args.config} 的 ui_port 或停止原服务")
        if not args.ui_only:
            if not shutil.which("cmake"):raise ValueError("缺少 CMake 3.21+；构建还需要 Ninja、编译器、pkg-config 和 FFTW3 开发库")
            identifier="bds-"+datetime.now().strftime("%Y%m%d-%H%M%S")+"-"+str(os.getpid())
            process_job(job,settings,identifier)
        print(f"北斗 UI：http://127.0.0.1:{port}/；按 Ctrl+C 停止本次服务。",flush=True)
        run_child([sys.executable,"-u",ROOT/"ui/server.py","--config",args.main_config.resolve(),"--port",port,"--signal","bds"])
        return 0
    except KeyboardInterrupt:
        print("\n北斗任务已停止；已生成的结果与日志保留。",file=sys.stderr)
        return 130
    except (OSError,ValueError,KeyError,subprocess.CalledProcessError) as exc:
        print(f"北斗启动失败：{exc}",file=sys.stderr)
        return exc.returncode if isinstance(exc,subprocess.CalledProcessError) and exc.returncode>0 else 1


if __name__=="__main__":sys.exit(main())
