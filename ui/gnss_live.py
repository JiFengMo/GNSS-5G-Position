"""Bounded reader for the B210 receiver's rotating, read-only JSONL records."""
from collections import defaultdict,deque
import copy
import json
import re
from pathlib import Path
import threading
import time

if __package__:
    from .gnss import curve,finite,uint
else:
    from gnss import curve,finite,uint

MAX_LINE=256*1024
BUDGET=4*1024*1024


class LiveStore:
    def __init__(self,directory):
        self.directory=Path(directory);self.lock=threading.Lock();self.path=None;self.offset=0
        self.run=None;self.runtime={};self.runtime_wall=0;self.invalid=0;self.backlog=0
        self.tracks=defaultdict(lambda:deque(maxlen=12000));self.frames=defaultdict(lambda:deque(maxlen=256))
        self.acquisitions={};self.events=deque(maxlen=240);self.history=deque(maxlen=800)
        self.position=None;self.observations=[];self.exclusions=[];self.origin=None;self.hardware_origin=None

    def accept(self,event):
        if not isinstance(event,dict) or event.get('schema')!=1 or not isinstance(event.get('data'),dict):raise ValueError('Bad live record')
        kind=event.get('kind');d=event['data']
        if kind=='config':
            if d.get('kind')!='b1c_live' or not finite(d.get('sample_rate_hz')) or d['sample_rate_hz']<=0:raise ValueError('Bad live configuration')
            prns=d.get('prns')
            if not isinstance(prns,list) or not prns or len(prns)>63 or len(set(prns))!=len(prns) or any(type(p)!=int or not 1<=p<=63 for p in prns):raise ValueError('Bad live PRNs')
            if not isinstance(d.get('run_id'),str) or d['run_id']!=event.get('run_id') or not isinstance(d.get('positioning'),dict) or not finite(d['positioning'].get('period_ms')) or d['positioning']['period_ms']<=0:raise ValueError('Bad live session metadata')
            if self.run and self.run['run_id']==d.get('run_id'):return
            self.run=copy.deepcopy(d);self.tracks.clear();self.frames.clear();self.acquisitions.clear();self.events.clear();self.history.clear()
            self.runtime={};self.runtime_wall=0;self.position=None;self.origin=None;self.observations=[];self.exclusions=[];self.hardware_origin=None
            return
        if not self.run or event.get('run_id')!=self.run['run_id']:raise ValueError('Live session mismatch')
        if kind=='runtime':
            if type(d.get('running'))!=bool or not isinstance(d.get('stage'),str):raise ValueError('Bad live runtime')
            if not finite(event.get('time_ms')):raise ValueError('Bad runtime wall clock')
            for k in ('processed_sample','received_samples','rx_sample','valid_fixes','valid_frames'):
                if k in d:uint(d,k)
            if 'lag_s' in d and (not finite(d['lag_s']) or d['lag_s']<0):raise ValueError('Bad runtime lag')
            self.runtime.update(d);self.runtime_wall=event['time_ms']
        elif kind=='hardware_origin':
            self.hardware_origin=uint(d,'hardware_sample')
        elif kind=='acquisition':
            p=d['prn']
            if p not in self.run['prns'] or d.get('acquired') not in (0,1):raise ValueError('Bad acquisition')
            self.acquisitions[p]=d
        elif kind=='tracking':
            p=d['prn']
            if p not in self.run['prns'] or d.get('valid') not in (0,1) or d.get('state') not in ('pull_in','locked','lost'):raise ValueError('Bad live epoch')
            for k in ('epoch','generation','absolute_sample','segment','sample_count'):uint(d,k)
            if not finite(d.get('time_s')) or not finite(d.get('end_time_s')) or d['end_time_s']<=d['time_s']:raise ValueError('Bad epoch time')
            if self.hardware_origin is not None:d['hardware_sample']=str(self.hardware_origin+uint(d,'absolute_sample'))
            if self.tracks[p] and d['time_s']<=self.tracks[p][-1]['time_s']:raise ValueError('Out-of-order live epoch')
            d=dict(d);previous=self.tracks[p][-1] if self.tracks[p] else None
            d['_display_segment']=previous.get('_display_segment',0) if previous else 0
            if previous and uint(previous,'absolute_sample')+uint(previous,'sample_count')!=uint(d,'absolute_sample'):
                d['_display_segment']+=1
            self.tracks[p].append(d)
        elif kind=='frame':
            p=d['prn']
            if p not in self.run['prns'] or d.get('valid') not in (0,1) or not finite(d.get('available_s')):raise ValueError('Bad live frame')
            for k in ('generation','segment','first_epoch','absolute_sample','end_sample'):uint(d,k)
            self.frames[p].append(d)
        elif kind=='position':
            p=d['solution'];obs=d['observations'];excluded=d['exclusions']
            if not isinstance(p,dict) or p.get('valid') not in (0,1) or not finite(p.get('time_s')) or not isinstance(obs,list) or len(obs)>63 or not isinstance(excluded,list) or len(excluded)>63:raise ValueError('Bad live position')
            if any(not isinstance(o,dict) or o.get('prn') not in self.run['prns'] or o.get('used') not in (0,1) for o in obs) or len({o['prn'] for o in obs})!=len(obs):raise ValueError('Bad live observations')
            if any(not isinstance(o,dict) or o.get('prn') not in self.run['prns'] or not isinstance(o.get('reason'),str) for o in excluded):raise ValueError('Bad live exclusions')
            if p['valid'] and not all(finite(p.get(k)) for k in ('x_m','y_m','z_m','latitude_deg','longitude_deg','height_m')):raise ValueError('Nonfinite position')
            if p['valid'] and (sum(bool(o.get('used')) for o in obs)!=p.get('satellites') or p['satellites']<4):raise ValueError('Bad live used satellites')
            self.position=p;self.observations=obs;self.exclusions=excluded;self.history.append(p)
            if p['valid'] and self.origin is None:self.origin={k:p[k] for k in ('x_m','y_m','z_m','latitude_deg','longitude_deg','height_m')}
        elif kind in ('event','gap'):
            if not finite(d.get('time_s')):raise ValueError('Bad live event time')
            if kind=='gap':
                if not isinstance(d.get('reason'),str):raise ValueError('Bad live gap')
                self.position=None;self.observations=[];self.exclusions=[]
                self.history.append(dict(time_s=d['time_s'],valid=0));d=dict(d,event=d['reason'],prn=0)
            self.events.append(d)
        else:raise ValueError('Unknown live event')

    def update(self):
        candidates=[]
        if self.directory.is_dir():
            for folder in self.directory.iterdir():
                if not folder.is_dir() or folder.is_symlink():continue
                for file in folder.glob('events-*.jsonl'):
                    if re.fullmatch(r'events-\d+\.jsonl',file.name) and file.is_file() and not file.is_symlink():
                        try:candidates.append((file.stat().st_mtime_ns,file))
                        except OSError:pass
        if not candidates:return
        path=max(candidates,key=lambda r:(r[0],r[1].parent.name,int(r[1].stem.split('-')[-1])))[1]
        if path!=self.path:
            if self.path and path.parent==self.path.parent and int(path.stem.split('-')[-1])>int(self.path.stem.split('-')[-1])+1 and self.position:
                self.history.append(dict(time_s=self.position['time_s'],valid=0))
            self.path=path;self.offset=0
        try:
            with path.open('rb') as source:
                if self.offset>path.stat().st_size:self.offset=0
                source.seek(self.offset);start=self.offset
                while source.tell()-start<BUDGET:
                    offset=source.tell();line=source.readline(MAX_LINE+1)
                    if not line:break
                    if not line.endswith(b'\n'):
                        if len(line)<=MAX_LINE:source.seek(offset);break
                        while line and not line.endswith(b'\n'):line=source.readline(MAX_LINE+1)
                        self.invalid+=1;continue
                    if len(line)>MAX_LINE:self.invalid+=1;continue
                    try:self.accept(json.loads(line,parse_constant=lambda _:None))
                    except (ValueError,TypeError,KeyError,OverflowError):self.invalid+=1
                self.offset=source.tell();self.backlog=max(0,path.stat().st_size-self.offset)
        except OSError:pass

    def snapshot(self,prn=None,window=30):
        if not finite(window) or not 0<window<=120:raise ValueError('Window must be 0..120 seconds')
        with self.lock:
            self.update()
            live=dict(self.runtime,connected=bool(self.runtime.get('running') and 0<=time.time()*1000-self.runtime_wall<3000),
                      invalid_lines=self.invalid,reader_backlog_bytes=self.backlog)
            if not self.run:return dict(waiting=True,live=live)
            fs=self.run['sample_rate_hz'];cursor=int(self.runtime.get('processed_sample','0'))/fs
            lag=self.runtime.get('lag_s',0);current=live['connected'] and finite(lag) and lag<=2 and not self.backlog
            live['current']=bool(current)
            present=sorted({p for p,r in self.acquisitions.items() if r.get('acquired')}|set(self.tracks)) or self.run['prns']
            selected=int(prn) if prn is not None else present[0]
            if selected not in self.run['prns']:raise ValueError('PRN not present in current session')
            if selected not in present:selected=present[0]
            pos=self.position;period=self.run['positioning']['period_ms']/1000
            position_current=bool(current and pos and 0<=cursor-pos['time_s']<=period*1.5)
            observations=self.observations if position_current else [];exclusions=self.exclusions if position_current else []
            by_prn={o['prn']:o for o in observations};reasons={o['prn']:o['reason'] for o in exclusions}
            satellites=[]
            for p in present:
                latest=self.tracks[p][-1] if self.tracks[p] else None
                track=latest if current and latest and -.01<=cursor-latest['time_s']<=.04 else None
                frames=[f for f in self.frames[p] if f['available_s']<=cursor and f.get('valid')]
                frame=frames[-1] if frames else None
                ready=bool(track and track['valid'] and frame and track['segment']==frame['segment'] and track['generation']==frame['generation'])
                obs=by_prn.get(p)
                satellites.append(dict(prn=p,tracking=track,last_tracking=latest,acquisition=self.acquisitions.get(p),frame=frame,navigation_ready=ready,
                    observation=obs,used=bool(position_current and pos['valid'] and obs and obs['used']),exclusion=reasons.get(p)))
            records=[dict(r,segment=(r['segment'],r['_display_segment'])) for r in self.tracks[selected] if cursor-window<=r['time_s']<=cursor]
            fields=('doppler_hz','cn0_db_hz','dll_error','pll_error','data_i_p','data_q_p','pilot_i_p','pilot_q_p','residual_frequency_hz')
            series={k:curve(records,k) for k in fields}
            metadata=copy.deepcopy(self.run);metadata.update(prns=present,start_s=0,end_s=cursor,origin=self.origin,
                valid_fixes=int(self.runtime.get('valid_fixes','0')),valid_frames=int(self.runtime.get('valid_frames','0')),
                tracking_epochs=sum(len(rows) for rows in self.tracks.values()),live=True)
            frames=[f for f in self.frames[selected] if f['available_s']<=cursor][::-1]
            return dict(run=metadata,cursor_s=cursor,selected_prn=selected,satellites=satellites,live=live,
                counts=dict(captured=sum(bool(r['acquired']) for r in self.acquisitions.values()),capture_attempts=len(self.acquisitions),
                    tracked=sum(bool(s['tracking'] and s['tracking']['valid']) for s in satellites),navigation_ready=sum(s['navigation_ready'] for s in satellites),
                    used=sum(s['used'] for s in satellites),received_frames=metadata['valid_frames']),
                position=pos,position_current=position_current,observations=observations,exclusions=exclusions,series=series,
                position_history=list(self.history),events=[r for r in self.events if r.get('prn') in (0,selected)][-60:],frames=frames[:20])

    def frame_page(self,prn,offset=0,limit=20):
        if offset<0 or not 1<=limit<=50:raise ValueError('Invalid frame page')
        with self.lock:
            self.update()
            if not self.run or prn not in self.run['prns']:raise ValueError('No current PRN')
            t=int(self.runtime.get('processed_sample','0'))/self.run['sample_rate_hz']
            frames=[r for r in self.frames[prn] if r['available_s']<=t][::-1]
            return dict(frames=frames[offset:offset+limit],total=len(frames),offset=offset,limit=limit)
