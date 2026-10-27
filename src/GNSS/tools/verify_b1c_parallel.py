#!/usr/bin/env python3
"""Run the integrated receiver on Linux; compare serial/parallel outputs.

Run from project root (build first):
  python3 src/GNSS/tools/verify_b1c_parallel.py --input /path/to/IQ \
    --label synthetic --output-dir output/b1c-check-NEW
Outputs: measurements.csv, comparisons.json, summary.json and per-run logs.
Next: return the whole result directory; this tool never opens a radio.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import signal
import statistics
import subprocess
import time


def digest(path):
    value=hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda:source.read(1024*1024),b''):value.update(block)
    return value.hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',required=True,type=Path)
    parser.add_argument('--label',required=True,choices=('synthetic','real'))
    parser.add_argument('--output-dir',required=True,type=Path)
    parser.add_argument('--bin-dir',type=Path,default=Path('build/gnss-offline/src/GNSS'))
    parser.add_argument('--input-format',choices=('ci8','cf32'),default='cf32')
    parser.add_argument('--sample-rate',type=float,default=4092000)
    parser.add_argument('--if-hz',type=float,default=0)
    parser.add_argument('--seconds',type=float,default=6)
    parser.add_argument('--prns',default='1-63')
    parser.add_argument('--doppler-min',type=float,default=-8000)
    parser.add_argument('--doppler-max',type=float,default=8000)
    parser.add_argument('--doppler-step',type=float,default=250)
    parser.add_argument('--fine-step',type=float,default=25)
    parser.add_argument('--repeats',type=int,default=3)
    parser.add_argument('--timeout',type=float,default=1200)
    args=parser.parse_args()
    if args.repeats<1 or args.repeats>20:parser.error('--repeats must be 1..20')
    source=args.input.resolve(strict=True)
    binaries={name:(args.bin_dir/name).resolve(strict=True) for name in ('bds-b1c-receiver','bds-b1c-track','bds-b1c-replay')}
    out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'inputs.json').write_text(json.dumps({**vars(args),'input':str(source),'input_sha256':digest(source),
        'binaries':{key:{'path':str(path),'sha256':digest(path)} for key,path in binaries.items()}},default=str,indent=2)+'\n')
    records=[];checks=[]
    common=['--input',str(source),'--input-format',args.input_format,'--sample-rate',str(args.sample_rate),'--if',str(args.if_hz)]
    search=['--doppler-min',str(args.doppler_min),'--doppler-max',str(args.doppler_max),
        '--doppler-step',str(args.doppler_step),'--fine-step',str(args.fine_step)]

    def save():
        columns=('run','kind','threads','repeat','returncode','wall_s','rss_kib')
        with (out/'measurements.csv').open('w',newline='') as stream:
            writer=csv.DictWriter(stream,fieldnames=columns);writer.writeheader();writer.writerows(records)
        (out/'comparisons.json').write_text(json.dumps(checks,indent=2)+'\n')

    def run(name,kind,threads,repeat,command,allowed=(0,)):
        command=list(map(str,command));(out/(name+'.command.json')).write_text(json.dumps(command,indent=2)+'\n')
        executable=command
        timer=Path('/usr/bin/time');rss=out/(name+'.rss.txt')
        if timer.is_file():executable=[str(timer),'-f','%M','-o',str(rss),*command]
        start=time.perf_counter()
        print(name,flush=True)
        with (out/(name+'.stdout.log')).open('w') as stdout,(out/(name+'.stderr.log')).open('w') as stderr:
            child=subprocess.Popen(executable,stdout=stdout,stderr=stderr,start_new_session=True)
            def stop():
                try:os.killpg(child.pid,signal.SIGTERM)
                except ProcessLookupError:pass
                try:child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    try:os.killpg(child.pid,signal.SIGKILL)
                    except ProcessLookupError:pass
                    child.wait()
            try:code=child.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                stop()
                records.append(dict(run=name,kind=kind,threads=threads,repeat=repeat,returncode='timeout',wall_s=time.perf_counter()-start,rss_kib=''))
                save();raise
            except BaseException:stop();raise
        peak=''
        if rss.exists():
            values=rss.read_text().splitlines()
            if values and values[-1].isdigit():peak=int(values[-1])
        records.append(dict(run=name,kind=kind,threads=threads,repeat=repeat,returncode=code,wall_s=time.perf_counter()-start,rss_kib=peak))
        save()
        if code not in allowed:raise RuntimeError(f'{name}: exit {code}; see stderr log')
        return code

    def compare(name,left,right):
        a,b=digest(left),digest(right);passed=a==b
        checks.append(dict(check=name,passed=passed,left=str(left),right=str(right),left_sha256=a,right_sha256=b));save()
        if not passed:raise RuntimeError(f'Exact comparison failed: {name}')

    reference=None;acquired=False
    for repeat in range(args.repeats):
        for threads in ((1,8) if repeat%2==0 else (8,1)):
            name=f'acq-t{threads}-r{repeat}';csv_path=out/(name+'.csv')
            code=run(name,'acquisition',threads,repeat,[binaries['bds-b1c-receiver'],*common,*search,'--prns',args.prns,'--threads',threads,'--output',csv_path],(0,7))
            if reference is None:reference=csv_path;acquired=code==0
            else:compare(name,reference,csv_path)
    if not acquired:raise RuntimeError('No acquired PRN: acquisition comparison saved; cannot validate tracking with invented seeds')
    for automatic in (False,True):
        reference_dir=None
        for repeat in range(args.repeats):
            order=(1,4,8) if repeat%2==0 else (8,4,1)
            for threads in order:
                name=f'track-auto{int(automatic)}-t{threads}-r{repeat}';directory=out/name
                command=[binaries['bds-b1c-track'],*common,'--acquisition-csv',reference,'--seconds',args.seconds,'--threads',threads,'--output-dir',directory]
                if not automatic:command+=['--no-reacquire']
                run(name,'tracking-auto' if automatic else 'tracking-no-reacquire',threads,repeat,command)
                if reference_dir is None:reference_dir=directory
                else:
                    names=sorted(p.name for p in reference_dir.glob('*.csv'))
                    if names!=sorted(p.name for p in directory.glob('*.csv')):raise RuntimeError('Different tracking output file set')
                    for filename in names:compare(name+'/'+filename,reference_dir/filename,directory/filename)
    for paced in (0,1):
        reference_events=None
        for repeat in range(args.repeats):
            for acq,track in ((1,1),(8,4),(8,8)):
                name=f'replay-paced{paced}-a{acq}-t{track}-r{repeat}';directory=out/name
                run(name,f'replay-paced{paced}',f'{acq}/{track}',repeat,[binaries['bds-b1c-replay'],*common,*search,'--prns',args.prns,
                    '--seconds',args.seconds,'--acquisition-threads',acq,'--tracking-threads',track,'--paced',paced,'--queue-seconds',16,'--output-dir',directory])
                summary=json.loads((directory/'summary.json').read_text())
                if int(summary['queue_dropped_samples']):
                    checks.append(dict(check=name,passed=None,reason='Dropped samples: do not compare different workloads or claim a speedup'));save();continue
                events=directory/'events.jsonl'
                if reference_events is None:reference_events=events
                else:compare(name+'/events',reference_events,events)
    groups={}
    for row in records:groups.setdefault((row['kind'],str(row['threads'])),[]).append(row['wall_s'])
    result={'complete':True,'comparisons_passed':all(c['passed'] is not False for c in checks),
        'skipped_comparisons':sum(c['passed'] is None for c in checks),
        'timing':'External process wall time including initialization and output; separate from earlier internal kernel timings.',
        'rss':'GNU time per-child max RSS in KiB; cross-check anomalous values with /proc/<pid>/status.',
        'groups':[{ 'kind':kind,'threads':threads,'median_s':statistics.median(values),'min_s':min(values),'max_s':max(values)} for (kind,threads),values in groups.items()]}
    (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
