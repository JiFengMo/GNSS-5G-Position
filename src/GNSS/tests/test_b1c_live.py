"""Transport-independent live causality and test-only RX lifecycle checks."""
import copy,csv,gzip,json,math
from pathlib import Path
import subprocess,sys,tempfile,time

core,mock,fixture=sys.argv[1:]
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);data=json.loads(gzip.decompress(Path(fixture).read_bytes()))
    records=[];columns=data['tracking_columns']
    for prn,values in data['tracking'].items():
        channel=[dict(zip(columns,r),prn=int(prn)) for r in values]
        # A short synthetic continuation lets causal decoding finish before
        # requesting the first fix; the committed MATLAB fixture is one frame.
        for j in range(40):
            old=channel[-1];r=copy.deepcopy(old);r['epoch']=int(old['epoch'])+1
            r['absolute_sample']=int(old['absolute_sample'])+int(old['sample_count'])
            r['rem_code_phase']=float(old['rem_code_phase'])+int(old['sample_count'])*float(old['code_rate_hz'])/18e6-10230
            r['sample_count']=math.ceil((10230-r['rem_code_phase'])*18e6/float(r['code_rate_hz']))
            r['integration_s']=r['sample_count']/18e6
            channel.append(r)
        records.extend(channel)
    records.sort(key=lambda r:int(r['absolute_sample'])+int(r['sample_count']))
    path=root/'epochs.csv'
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=[*columns,'prn'],lineterminator='\n');writer.writeheader();writer.writerows(records)
    result=subprocess.run([core,str(path),'18000000'],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    events=[json.loads(line) for line in result.stdout.splitlines()]
    frames={e['data']['prn']:e['data'] for e in events if e['kind']=='frame' and e['data']['valid']}
    assert len(frames)==7,len(frames)
    fixes=[e['data'] for e in events if e['kind']=='position' and e['data']['solution']['valid']]
    assert fixes,'Causal path should solve after frames are available'
    for p in fixes:
        assert 39<p['solution']['latitude_deg']<41
        for observation in p['observations']:
            if observation['used']:
                frame=frames[observation['prn']]
                assert frame['available_s']<=p['solution']['time_s']
                assert frame['generation']==observation['generation']
    print('PASS: original MATLAB frame vectors feed online decoder; causal fixes use only already-received continuous frames')

    def command(name,args):
        return [mock,'--device-args',args,'--output-dir',str(root/name),'--run-id',name,
                '--prns','23','--doppler-min','0','--doppler-max','0','--queue-seconds','.1']
    p=subprocess.Popen(command('normal','fractional'),stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            if p.poll() is not None:raise RuntimeError(p.communicate())
            files=list((root/'normal').glob('*.jsonl'))
            if files and 'runtime' in files[0].read_text() and 'hardware_origin' in files[0].read_text():break
            time.sleep(.05)
        else:raise RuntimeError('No live records')
    finally:
        p.terminate();out,err=p.communicate(timeout=10)
    assert p.returncode==0,(p.returncode,err)
    assert 'actual=4092000.003' in err,err
    events=[json.loads(s) for f in (root/'normal').glob('*.jsonl') for s in f.read_text().splitlines()]
    assert any(e['kind']=='hardware_origin' and int(e['data']['hardware_sample'])>2**53 for e in events)
    assert any(e['kind']=='runtime' and int(e['data'].get('rx_overflows','0'))>0 for e in events)
    assert events[-1]['kind']=='runtime' and not events[-1]['data']['running']
    config=next(e['data'] for e in events if e['kind']=='config')
    assert config['sample_rate_hz']==4092000.0037
    assert config['device']['requested_sample_rate_hz']==4092000
    result=subprocess.run(command('backwards','backward'),capture_output=True,text=True,timeout=10)
    assert result.returncode!=0 and 'backwards' in result.stderr,result.stderr
    result=subprocess.run(command('missing','mock')+['--serial','missing'],capture_output=True,text=True,timeout=10)
    assert result.returncode!=0 and not (root/'missing').exists()
    result=subprocess.run(command('bad-rate','bad-rate'),capture_output=True,text=True,timeout=10)
    assert result.returncode!=0 and 'actual=4092020' in result.stderr and 'tolerance' in result.stderr,result.stderr
    assert not (root/'bad-rate').exists()
    print('PASS: test-only transport exercises hardware counters, overflow/gap handling, receiver shutdown and missing-device failure')
