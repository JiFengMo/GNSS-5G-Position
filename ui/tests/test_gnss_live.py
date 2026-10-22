"""Read-only live UI checks with explicitly synthetic display records."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request,urlopen

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from gnss_live import LiveStore
from server import ROOT,load_settings,MonitorStore,make_server


def record(kind,data,run='test-only-display'):
    return dict(schema=1,run_id=run,time_ms=time.time()*1000,kind=kind,data=data)


def fixture():
    config=dict(kind='b1c_live',run_id='test-only-display',name='TEST ONLY / synthetic UI display',sample_rate_hz=4092000,
        prns=[23,25,27,28],tracking=dict(if_hz=0,pll_bandwidth_hz=18,dll_bandwidth_hz=1,early_late_offset_chips=.06,quality_epochs=50),
        navigation=dict(ldpc_error_correction=False),positioning=dict(period_ms=200,elevation_mask_deg=5,troposphere=True),
        device=dict(args='test-only-mock',tuned_hz=1575420000,antenna='RX2',clock_source='external',time_source='external'))
    events=[record('config',config),record('hardware_origin',dict(hardware_sample=str(2**53+19)))]
    for p in config['prns']:
        events.append(record('acquisition',dict(prn=p,acquired=1,peak_metric=12,doppler_hz=250,code_phase_samples='12')))
        for t in (19.98,19.99):
            events.append(record('tracking',dict(prn=p,epoch=str(round(t*100)),generation='0',segment='1',
                absolute_sample=str(round(t*4092000)),sample_count=40920,time_s=t,end_time_s=t+.01,valid=1,state='locked',
                cn0_db_hz=43,doppler_hz=250,dll_error=.001,pll_error=.001,data_i_p=1,data_q_p=.1,pilot_i_p=.1,pilot_q_p=1,
                data_i_e=.8,data_q_e=.1,data_i_l=.8,data_q_l=.1,pilot_i_e=.1,pilot_q_e=.8,pilot_i_l=.1,pilot_q_l=.8,residual_frequency_hz=.2)))
        for j in range(25):
            events.append(record('frame',dict(prn=p,generation='0',segment='1',first_epoch=str(j),absolute_sample='0',end_sample=str(18*4092000),
                time_s=0,available_s=18,valid=1,status='ok',crc2_ok=1,crc3_ok=1,ephemeris=dict(PRN=p,WN=832,PageID=j))))
    solution=dict(time_s=19.9,available_s=19.9,valid=1,status='ok',satellites=4,x_m=-1260000,y_m=-4720000,z_m=4080000,
        latitude_deg=40,longitude_deg=-105,height_m=1600,pdop=2,residual_rms_m=3)
    events.append(record('position',dict(solution=solution,observations=[dict(prn=p,used=1) for p in config['prns']],exclusions=[])))
    events.append(record('runtime',dict(running=True,stage='tracking',processed_sample=str(20*4092000),received_samples=str(20*4092000),
        lag_s=0,valid_fixes='1',valid_frames='100',reference_locked=True,pps_detected=True)))
    return events


def write(path,events,mode='w'):
    with path.open(mode,encoding='utf-8') as f:
        for e in events:f.write(json.dumps(e)+'\n')


class GNSSLiveTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.folder=self.root/'run';self.folder.mkdir();self.path=self.folder/'events-000001.jsonl'
        self.events=fixture();write(self.path,self.events);self.store=LiveStore(self.root)
    def tearDown(self):self.temp.cleanup()

    def test_fresh_state_and_uint64_and_frames(self):
        s=self.store.snapshot();self.assertTrue(s['live']['current']);self.assertTrue(s['position_current'])
        self.assertEqual(s['counts']['used'],4);self.assertEqual(s['counts']['navigation_ready'],4)
        latest=s['satellites'][0]['tracking']
        self.assertEqual(int(latest['hardware_sample']),2**53+19+int(latest['absolute_sample']))
        self.assertEqual(self.store.frame_page(23,20)['frames'][0]['ephemeris']['PageID'],4)
        with self.assertRaises(ValueError):self.store.snapshot(prn=64)

    def test_stale_lagged_and_stopped_hide_current_results(self):
        for stale,lag,running in [(True,0,True),(False,3,True),(False,0,False)]:
            e=record('runtime',dict(running=running,stage='tracking',lag_s=lag))
            if stale:e['time_ms']-=4000
            write(self.path,[e],'a');s=self.store.snapshot()
            self.assertFalse(s['live']['current']);self.assertFalse(s['position_current'])
            self.assertEqual(s['counts']['tracked'],0);self.assertEqual(s['counts']['used'],0)

    def test_gap_clears_pvt_and_generation_does_not_reuse_frame(self):
        self.store.snapshot();self.store.accept(record('gap',dict(time_s=20,reason='rx_gap')))
        self.assertIsNone(self.store.snapshot()['position']);self.assertEqual(self.store.history[-1]['valid'],0)
        t=copy.deepcopy(self.events[3]['data']);t.update(time_s=20,end_time_s=20.01,absolute_sample=str(20*4092000),generation='1')
        self.store.accept(record('tracking',t));s=self.store.snapshot()
        self.assertFalse(s['satellites'][0]['navigation_ready'])

    def test_partial_and_bad_lines_and_rotation(self):
        with self.path.open('ab') as f:f.write(b'{bad}\n'+json.dumps(record('frame',dict(prn=23,valid=1,available_s=0))).encode()+b'\n')
        partial=json.dumps(record('runtime',dict(running=False,stage='stopped')))
        with self.path.open('a') as f:f.write(partial)
        s=self.store.snapshot();self.assertEqual(s['live']['invalid_lines'],2);self.assertTrue(s['live']['connected'])
        with self.path.open('a') as f:f.write('\n')
        self.assertFalse(self.store.snapshot()['live']['connected'])
        nextfile=self.folder/'events-000003.jsonl';write(nextfile,[self.events[0],record('runtime',dict(running=True,stage='tracking'))])
        self.assertEqual(self.store.snapshot()['counts']['received_frames'],100)
        self.assertEqual(self.store.history[-1]['valid'],0)

    def test_reader_gaps_break_plots_without_changing_nav_segment(self):
        self.store.snapshot();t=copy.deepcopy(self.events[4]['data'])
        t.update(time_s=20.01,end_time_s=20.02,absolute_sample=str(round(20.01*4092000)))
        self.store.accept(record('tracking',t));self.store.accept(record('runtime',dict(running=True,stage='tracking',processed_sample=str(round(20.02*4092000)))))
        s=self.store.snapshot();self.assertIn(None,s['series']['doppler_hz']['y'])
        self.assertEqual(s['satellites'][0]['tracking']['segment'],'1')

    def test_new_session_resets_and_history_is_bounded(self):
        self.store.snapshot()
        for j in range(900):self.store.accept(self.events[-2])
        self.assertEqual(len(self.store.history),800)
        e=copy.deepcopy(self.events[0]);e['run_id']=e['data']['run_id']='new'
        self.store.accept(e);s=self.store.snapshot();self.assertEqual(s['counts']['tracked'],0);self.assertIsNone(s['position'])

    def test_http_is_read_only_and_live_default_is_explicit(self):
        settings=load_settings(ROOT/'config/config.f');settings.update(gnss_live_directory=self.root,default_signal='bds',bds_live=True)
        server=make_server(settings,MonitorStore(settings),port=0);thread=threading.Thread(target=server.serve_forever);thread.start()
        base=f'http://127.0.0.1:{server.server_address[1]}'
        try:
            with urlopen(base+'/') as f:self.assertIn(b'data-bds-source="live"',f.read())
            with urlopen(base+'/api/gnss/live') as f:self.assertEqual(json.load(f)['counts']['used'],4)
            with self.assertRaises(HTTPError) as error:urlopen(Request(base+'/api/gnss/live',data=b'{}',method='POST'))
            self.assertEqual(error.exception.code,501);error.exception.close()
        finally:server.shutdown();server.server_close();thread.join()


if __name__=='__main__':
    if '--serve' in sys.argv:
        # Isolated browser-QA fixture, never under log/ or connected to hardware.
        root=Path(sys.argv[2]).resolve();folder=root/'test-only';folder.mkdir(parents=True,exist_ok=False)
        write(folder/'events-000001.jsonl',fixture())
        settings=load_settings(ROOT/'config/config.f');settings.update(gnss_live_directory=root,default_signal='bds',bds_live=True)
        server=make_server(settings,MonitorStore(settings),port=int(sys.argv[3]))
        print(str(folder/'events-000001.jsonl'),flush=True)
        try:server.serve_forever()
        finally:server.server_close()
    else:unittest.main()
