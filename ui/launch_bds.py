#!/usr/bin/env python3
"""B210 live receiver launcher. Run: sh run_BDS_only.sh [--offline|--ui-only|--check].

Default: builds gnss-radio, opens the configured B210 and starts BDS live UI.
--offline uses config/GNSS/offline.f and the validated file-processing pipeline.
Outputs live JSONL in log/gnss-live/ and task logs in log/launcher/.
Next: open the printed UI address. Ctrl+C stops this task's receiver and UI.
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
import time
from urllib.request import urlopen

from gnss import import_run
from server import ROOT,load_settings,read_config

JOB_KEYS={"input_file","input_format","sample_rate_hz","if_hz","seconds","prns","ui_port"}


class BDSProcessError(subprocess.CalledProcessError):
    """Keep the exit status for callers without dumping the entire command."""
    def __init__(self,code,command,message):
        super().__init__(code,command);self.message=message
    def __str__(self):return self.message


def process_error(stage,command,code,log_path,config_path=None,job=None):
    try:
        with log_path.open(encoding='utf-8',errors='replace') as source:tail=''.join(deque(source,maxlen=80))
    except OSError:tail=''
    issues=[
        (('BDS_REF_UNLOCKED','External 10 MHz reference did not lock'), 'REF_UNLOCKED',
         '配置要求外部 10 MHz 参考，但设备在等待期间未报告锁定。',
         '如果没有接外部参考：将 clock_source 改为 internal；如果也没有接外部 PPS，将 time_source 一并改为 internal。\n如果需要外部同步：检查所选设备的 10 MHz 输入、参考源输出及连接，再重新启动。'),
        (('BDS_REF_LOST','External 10 MHz reference lost lock'), 'REF_LOST',
         '采样过程中，外部 10 MHz 参考失去锁定。', '检查参考源输出和线缆连接，恢复后重新启动。'),
        (('BDS_PPS_MISSING','External PPS not detected'), 'PPS_MISSING',
         '配置要求外部 PPS，但启动时未检测到 PPS 跳变。',
         '未接外部 PPS 时，将 time_source 改为 internal；已接入时，检查 PPS 输出与设备 PPS 接口。'),
        (('BDS_PPS_LOST','External PPS stopped'), 'PPS_LOST',
         '采样过程中，外部 PPS 停止更新。', '检查 PPS 源和线缆，恢复后重新启动。'),
        (('BDS_DEVICE_NOT_FOUND','No connected B210 matches','No matching serial'), 'DEVICE_NOT_FOUND',
         '没有发现符合 device_args / serial 的可用 B210。',
         '运行 uhd_find_devices 核对设备与序列号；检查 USB、供电，以及设备是否被其他程序占用。填写 serial 后不会回退选择其他设备。'),
        (('BDS_RATE_UNSUPPORTED','Actual RX rate exceeds','noninteger 10 ms code period','Actual RX sample rate/queue'), 'RATE_UNSUPPORTED',
         'UHD 回读的实际采样率或对应队列大小超出当前接收链路支持范围。',
         '核对下方 BDS RX rate 的请求值和实际值，以及 sample_rate_hz、master_clock_rate_hz、queue_seconds。若仍异常，请提供这几行和完整接收日志。'),
        (('BDS_RX_TIMEOUT','BDS RX timed out'), 'RX_TIMEOUT',
         '接收流连续超时，未能持续收到 IQ 数据。', '检查 USB 连接、供电及 UHD 原始错误；确认设备未断开后重新启动。'),
        (('RX hardware time moved backwards',), 'TIME_BACKWARDS',
         '接收硬件时间戳发生倒退，当前采样不能视为连续数据。', '检查设备时钟/复位及是否有其他程序控制设备，再重新启动。'),
    ]
    tag='PROCESS_EXIT';reason=f'{stage}异常退出（退出码 {code}），现有日志尚不能明确定位原因。'
    action='查看下方完整日志；请提供末尾的原始错误，进一步定位。'
    for markers,tag_value,reason_value,action_value in issues:
        if any(marker in tail for marker in markers):tag,reason,action=tag_value,reason_value,action_value;break
    lines=[f'[{tag}] {reason}',f'失败阶段：{stage}（退出码 {code}）']
    if config_path:lines.append('配置文件：'+str(Path(config_path).resolve()))
    if job:
        lines.append(f"本次加载值：clock_source = {job['clock_source']}；time_source = {job['time_source']}；serial = {job['serial'] or '留空（默认发现设备）'}")
    # Show useful numerical/device evidence, not the surrounding UHD banner.
    evidence=[line for line in tail.splitlines() if line.startswith(('BDS selected device:','BDS RX rate:'))]
    errors=[line for line in tail.splitlines() if line.strip()]
    if errors:lines.append('原始错误：'+errors[-1][:1000])
    lines.extend(evidence[-2:]);lines.extend(['处理办法：'+action,'完整日志：'+str(log_path)])
    return BDSProcessError(code or 1,command,'\n'.join(lines))


def radio_settings(path):
    job=read_config(path,allow_empty_keys={'serial'})
    keys={'device','device_args','serial','center_frequency_hz','sample_rate_hz','bandwidth_hz','gain_db','clock_source','time_source',
          'signal','prn_list','doppler_min_hz','doppler_max_hz','doppler_step_hz','coherent_integration_ms',
          'if_hz','master_clock_rate_hz','rx_antenna','fine_step_hz','acquisition_threshold','queue_seconds','ui_port',
          'dll_bandwidth_hz','pll_bandwidth_hz','early_late_chips','position_period_ms','elevation_mask_deg'}
    if set(job)!=keys:raise ValueError(f'Unknown/missing BDS radio settings: {sorted(set(job)^keys)}')
    if job['device']!='usrp_b210' or job['signal']!='BDS_B1C':raise ValueError('Only B210 / BDS_B1C is supported')
    for key in ('clock_source','time_source'):
        if job[key] not in ('internal','external'):raise ValueError(f'{key} = {job[key]!r} 无效；可选 internal（内部来源）或 external（外部来源）')
    if not job['device_args'] or not job['rx_antenna']:raise ValueError('BDS device and antenna must be selected')
    if job['serial'] and not re.fullmatch(r'[A-Za-z0-9_-]+',job['serial']):raise ValueError('Invalid B210 serial')
    if any(p.strip().split('=',1)[0]=='serial' for p in job['device_args'].split(',')):
        raise ValueError('Set the dedicated serial option; do not put serial in device_args')
    for k in keys-{'device','device_args','serial','clock_source','time_source','signal','prn_list','rx_antenna'}:
        job[k]=float(job[k])
        if not math.isfinite(job[k]):raise ValueError(f'Nonfinite {k}')
    fs=job['sample_rate_hz']
    if not 2046000<=fs<=1e8 or abs(fs*.01-round(fs*.01))>1e-7:raise ValueError('Invalid BDS sampling rate')
    if job['coherent_integration_ms']!=10:raise ValueError('Online tracking/navigation requires 10 ms code epochs')
    if not 1024<=job['ui_port']<=65535 or job['ui_port']!=int(job['ui_port']):raise ValueError('Invalid ui_port')
    if not .1<=job['queue_seconds']<=60 or fs*job['queue_seconds']*8>2*1024**3:raise ValueError('RX queue must be .1..60 s and at most 2 GiB')
    if not 0<job['pll_bandwidth_hz']<=18 or not 0<job['dll_bandwidth_hz']<=10 or not 0<job['early_late_chips']<.5:raise ValueError('Invalid tracking loop settings')
    if not .01<=job['position_period_ms']<=10000 or abs(fs*job['position_period_ms']/1000-round(fs*job['position_period_ms']/1000))>1e-7:raise ValueError('Invalid position period')
    if not -90<=job['elevation_mask_deg']<90 or not 0<job['bandwidth_hz']<=56e6 or not 0<=job['gain_db']<=76:raise ValueError('Invalid RF/position settings')
    if not 70e6<=job['center_frequency_hz']<=6e9 or not 5e6<=job['master_clock_rate_hz']<=61.44e6:raise ValueError('Invalid B210 frequency/master clock')
    coarse,fine=job['doppler_step_hz'],job['fine_step_hz']
    if coarse<=0 or fine<=0 or not 1<=coarse/fine<=1000 or abs(coarse/fine-round(coarse/fine))>1e-7 or job['doppler_max_hz']<job['doppler_min_hz'] or (job['doppler_max_hz']-job['doppler_min_hz'])/coarse>20000 or abs((job['doppler_max_hz']-job['doppler_min_hz'])/coarse-round((job['doppler_max_hz']-job['doppler_min_hz'])/coarse))>1e-7:raise ValueError('Invalid Doppler search grid')
    if job['acquisition_threshold']<=0 or any(abs(job['if_hz']+job[k])+coarse>=fs/2 for k in ('doppler_min_hz','doppler_max_hz')):raise ValueError('Invalid acquisition threshold or aliased search frequency')
    prns=[]
    for item in job['prn_list'].split(','):
        if not re.fullmatch(r'[0-9]+(?:-[0-9]+)?',item):raise ValueError('Invalid PRN list')
        a,b=map(int,item.split('-')) if '-' in item else (int(item),int(item))
        if not 1<=a<=b<=63:raise ValueError('PRNs must be 1..63')
        prns.extend(range(a,b+1))
    if len(set(prns))!=len(prns):raise ValueError('Duplicate PRNs')
    job['ui_port']=int(job['ui_port']);return job


def radio_command(job,identifier):
    names={'device_args':'device-args','sample_rate_hz':'sample-rate','center_frequency_hz':'frequency','if_hz':'if',
        'master_clock_rate_hz':'master-clock-rate','bandwidth_hz':'bandwidth','gain_db':'gain','clock_source':'clock-source',
        'time_source':'time-source','rx_antenna':'antenna','prn_list':'prns','doppler_min_hz':'doppler-min','doppler_max_hz':'doppler-max',
        'doppler_step_hz':'doppler-step','fine_step_hz':'fine-step','acquisition_threshold':'threshold','queue_seconds':'queue-seconds',
        'dll_bandwidth_hz':'dll-bandwidth','pll_bandwidth_hz':'pll-bandwidth','early_late_chips':'early-late',
        'position_period_ms':'period-ms','elevation_mask_deg':'elevation-mask'}
    command=[ROOT/'build/gnss-radio/src/GNSS/bds-b1c-live','--output-dir',ROOT/'log/gnss-live'/identifier,'--run-id',identifier]
    for key,option in names.items():command.extend(['--'+option,job[key]])
    if job['serial']:command.extend(['--serial',job['serial']])
    return list(map(str,command))


def supervise_live(command,ui,logs,port,config_path=None,job=None):
    children=[];outputs=[]
    try:
        for name,args in [('ui',ui),('receiver',command)]:
            output=(logs/(name+'.log')).open('w',encoding='utf-8');outputs.append(output)
            child=subprocess.Popen(list(map(str,args)),cwd=ROOT,stdout=output,stderr=subprocess.STDOUT);children.append(child)
            if name=='ui':
                deadline=time.monotonic()+10
                while True:
                    if child.poll() is not None:raise process_error('北斗网页服务启动',child.args,child.returncode,logs/'ui.log',config_path,job)
                    try:
                        with urlopen(f'http://127.0.0.1:{port}/api/bootstrap',timeout=.5) as response:
                            if response.status==200:break
                    except OSError:pass
                    if time.monotonic()>deadline:raise RuntimeError('北斗网页服务 10 秒内未就绪；检查端口与完整日志：'+str(logs/'ui.log'))
                    time.sleep(.1)
        while True:
            for name,child in zip(('UI','USRP'),children):
                code=child.poll()
                if code is not None:
                    path=logs/('ui.log' if name=='UI' else 'receiver.log')
                    if code:raise process_error('USRP 接收与处理' if name=='USRP' else '北斗网页服务',child.args,code,path,config_path,job)
                    return
            time.sleep(.2)
    finally:
        for child in children:
            if child.poll() is None:child.terminate()
        for child in children:
            try:child.wait(timeout=10)
            except subprocess.TimeoutExpired:child.kill();child.wait()
        for output in outputs:output.close()


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
        stage='编译' if '--build' in command else '构建配置' if '--preset' in command else Path(str(command[0])).name
        if log_path:raise process_error(stage,command,status,log_path)
        raise BDSProcessError(status,command,f'{stage}异常退出（退出码 {status}）；请查看上方进程输出。')


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
    parser.add_argument("--offline",action="store_true",help="Use IQ files for algorithm verification; never opens USRP")
    parser.add_argument("--config",type=Path,help="Override BDS radio configuration (or offline job with --offline)")
    parser.add_argument("--main-config",type=Path,default=ROOT/"config/config.f",help="Shared UI configuration entry")
    args=parser.parse_args()
    def terminate(_signum,_frame):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,terminate)
    try:
        settings=load_settings(args.main_config)
        if args.config is None:
            main=read_config(args.main_config)
            args.config=ROOT/'config/GNSS/offline.f' if args.offline else (args.main_config.parent/main['gnss_config']).resolve()
        if args.ui_only:
            job={'ui_port':int(read_config(args.config,allow_empty_keys={'serial'})['ui_port'])}
            if not 1024<=job['ui_port']<=65535:raise ValueError('Invalid ui_port')
        else:job=job_settings(args.config) if args.offline else radio_settings(args.config)
        port=job["ui_port"]
        if args.check:
            print(f"北斗配置检查通过；{'离线 IQ：'+str(job['input_file']) if args.offline else 'USRP：'+job['device_args']}；网页端口：{port}；未打开设备")
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
            if args.offline:process_job(job,settings,identifier)
            else:
                logs=ROOT/'log/launcher'/identifier;logs.mkdir(parents=True,exist_ok=False)
                print(f"已加载北斗配置：{args.config.resolve()}\n时钟设置：clock_source = {job['clock_source']}；time_source = {job['time_source']}；serial = {job['serial'] or '留空（默认发现设备）'}",flush=True)
                run_child(['cmake','--preset','gnss-radio'],logs/'configure.log')
                run_child(['cmake','--build','--preset','gnss-radio','--parallel','2'],logs/'build.log')
                print(f'准备启动北斗实采，设备初始化中；网页地址：http://127.0.0.1:{port}/；运行日志：{logs}；Ctrl+C 停止本次 USRP 和 UI',flush=True)
                ui=[sys.executable,'-u',ROOT/'ui/server.py','--config',args.main_config.resolve(),'--port',port,'--signal','bds','--bds-source','live']
                supervise_live(radio_command(job,identifier),ui,logs,port,args.config,job)
                return 0
        print(f"北斗 UI：http://127.0.0.1:{port}/；按 Ctrl+C 停止本次服务。",flush=True)
        run_child([sys.executable,"-u",ROOT/"ui/server.py","--config",args.main_config.resolve(),"--port",port,"--signal","bds"])
        return 0
    except KeyboardInterrupt:
        print("\n北斗任务已停止；已生成的结果与日志保留。",file=sys.stderr)
        return 130
    except (OSError,ValueError,KeyError,RuntimeError,subprocess.CalledProcessError) as exc:
        detail=str(exc) if not isinstance(exc,subprocess.CalledProcessError) or isinstance(exc,BDSProcessError) else f'{Path(str(exc.cmd[0])).name}异常退出（退出码 {exc.returncode}）；请查看该阶段日志。'
        if not isinstance(exc,BDSProcessError):detail+='\n配置文件：'+str((args.config or args.main_config).resolve())
        print(f"北斗任务失败：\n{detail}",file=sys.stderr)
        return exc.returncode if isinstance(exc,subprocess.CalledProcessError) and exc.returncode>0 else 1


if __name__=="__main__":sys.exit(main())
