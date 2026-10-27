import importlib
import io
from contextlib import redirect_stderr,redirect_stdout
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch,Mock,MagicMock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import launch_bds
from server import load_settings,MonitorStore,make_server


class BDSLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.iq=self.root/'record with spaces.bin';self.iq.write_bytes(bytes(100000))
        self.config=self.root/'offline.f'
        self.settings=dict(input_file=str(self.iq),input_format='ci8',sample_rate_hz='2046000',if_hz='20000',seconds='.02',prns='23,25-28',ui_port='18766')
        self.write()

    def tearDown(self):self.temp.cleanup()

    def write(self):self.config.write_text(''.join(f'{k} = {v}\n' for k,v in self.settings.items()),encoding='utf-8')

    def test_job_validation_and_replay_without_iq(self):
        job=launch_bds.job_settings(self.config)
        self.assertEqual(job['input_file'],self.iq)
        for key,value in [('seconds','.015'),('sample_rate_hz','nan'),('input_format','bad'),('prns','23,23'),('ui_port','0')]:
            with self.subTest(key=key):
                original=self.settings[key];self.settings[key]=value;self.write()
                with self.assertRaises(ValueError):launch_bds.job_settings(self.config)
                self.settings[key]=original
        self.settings['input_file']='missing';self.write()
        self.assertEqual(launch_bds.job_settings(self.config,process=False)['ui_port'],18766)
        with self.assertRaises(ValueError):launch_bds.job_settings(self.config)

    def test_pipeline_uses_own_capture_and_imports_only_after_success(self):
        calls=[]
        def run(command,log):calls.append((list(map(str,command)),log))
        importer=Mock(return_value=dict(prns=[23],valid_frames=1,valid_fixes=2))
        with patch.object(launch_bds,'ROOT',self.root):
            result=launch_bds.process_job(launch_bds.job_settings(self.config),{'gnss_directory':self.root/'ui'},'test-run',run,importer)
        self.assertEqual(len(calls),6)
        self.assertEqual([Path(c[0][0]).name for c in calls[2:]],['bds-b1c-receiver','bds-b1c-track','bds-b1c-nav','bds-b1c-position'])
        acquisition=calls[2][0][calls[2][0].index('--output')+1]
        self.assertEqual(calls[3][0][calls[3][0].index('--acquisition-csv')+1],acquisition)
        self.assertIn(str(self.iq),calls[2][0])
        self.assertEqual(result,self.root/'ui/test-run')
        self.assertEqual(importer.call_args.kwargs['acquisition'],Path(acquisition))

    def test_pipeline_failure_does_not_import_or_start_next_stage(self):
        calls=[];importer=Mock()
        def fail(command,log):
            calls.append(command)
            if len(calls)==3:raise subprocess.CalledProcessError(7,command)
        with patch.object(launch_bds,'ROOT',self.root),self.assertRaises(subprocess.CalledProcessError):
            launch_bds.process_job(launch_bds.job_settings(self.config),{'gnss_directory':self.root/'ui'},'failed-run',fail,importer)
        self.assertEqual(len(calls),3);importer.assert_not_called()

    def test_worker_options_are_optional_validated_and_forwarded(self):
        job=launch_bds.job_settings(self.config)
        self.assertEqual((job['acquisition_threads'],job['tracking_threads']),(1,1))
        self.settings.update(acquisition_threads='8',tracking_threads='4');self.write()
        job=launch_bds.job_settings(self.config)
        self.assertEqual((job['acquisition_threads'],job['tracking_threads']),(8,4))
        calls=[];importer=Mock(return_value=dict(prns=[],valid_frames=0,valid_fixes=0))
        with patch.object(launch_bds,'ROOT',self.root):
            launch_bds.process_job(job,{'gnss_directory':self.root/'ui'},'parallel-run',lambda c,l:calls.append(list(map(str,c))),importer)
        self.assertEqual(calls[2][calls[2].index('--threads')+1],'8')
        self.assertEqual(calls[3][calls[3].index('--threads')+1],'4')
        for value in ('0','64','1.5','nan'):
            self.settings['tracking_threads']=value;self.write()
            with self.assertRaises(ValueError):launch_bds.job_settings(self.config)

    def test_interruption_stops_current_child(self):
        child=Mock();child.wait.side_effect=[KeyboardInterrupt,0]
        with patch.object(launch_bds.subprocess,'Popen',return_value=child),self.assertRaises(KeyboardInterrupt):
            launch_bds.run_child(['example'])
        child.terminate.assert_called_once();self.assertEqual(child.wait.call_count,2)

    def test_radio_config_serial_and_no_iq_file_argument(self):
        source=Path(__file__).resolve().parents[2]/'config/GNSS/bds_b1c.f'
        content=source.read_text(encoding='utf-8-sig');self.config.write_text(content,encoding='utf-8')
        job=launch_bds.radio_settings(self.config);command=launch_bds.radio_command(job,'test-only')
        self.assertNotIn('--serial',command);self.assertNotIn('--input',command)
        self.assertEqual(job['clock_source'],'external');self.assertEqual(job['time_source'],'external')
        job['serial']='1234567';command=launch_bds.radio_command(job,'test-only')
        self.assertEqual(command[command.index('--serial')+1],'1234567')
        for old,new in [('device_args = type=b200','device_args = type=b200,serial=duplicate'),('queue_seconds = 16','queue_seconds = nan'),('pll_bandwidth_hz = 18','pll_bandwidth_hz = 20')]:
            self.config.write_text(content.replace(old,new),encoding='utf-8')
            with self.assertRaises(ValueError):launch_bds.radio_settings(self.config)

    def test_receiver_failure_stops_own_ui(self):
        ui=Mock();ui.poll.return_value=None
        rx=Mock();rx.poll.return_value=3;rx.args=['test-only-receiver']
        response=MagicMock();response.__enter__.return_value.status=200
        with patch.object(launch_bds.subprocess,'Popen',side_effect=[ui,rx]),patch.object(launch_bds,'urlopen',return_value=response),self.assertRaises(subprocess.CalledProcessError):
            launch_bds.supervise_live(['test-only'],['test-only-ui'],self.root,18766)
        ui.terminate.assert_called_once();rx.terminate.assert_not_called()

    def test_clock_error_explains_loaded_values_config_and_recovery(self):
        log=self.root/'receiver.log'
        raw='BDS live receiver: [BDS_REF_UNLOCKED] External 10 MHz reference did not lock (clock_source=external, ref_locked=false, wait=3s)'
        log.write_text('[INFO] [UHD] lots of startup noise\nBDS selected device: serial=example\n'+raw+'\n',encoding='utf-8')
        job=dict(clock_source='external',time_source='external',serial='')
        error=launch_bds.process_error('USRP 接收与处理',['receiver','--many','arguments'],2,log,self.config,job)
        text=str(error)
        for expected in ('[REF_UNLOCKED]','未报告锁定','clock_source = external','time_source = external',str(self.config.resolve()),'internal','serial=example',str(log),raw):self.assertIn(expected,text)
        self.assertNotIn('[INFO]',text);self.assertNotIn('--many',text)
        self.assertEqual(error.returncode,2);self.assertEqual(log.read_text(encoding='utf-8').splitlines()[-1],raw)

    def test_other_receiver_errors_have_distinct_guidance_and_unknowns_keep_evidence(self):
        log=self.root/'receiver.log'
        for raw,tag in [('External PPS not detected','PPS_MISSING'),('No connected B210 matches device_args/serial','DEVICE_NOT_FOUND'),
                        ('External 10 MHz reference lost lock','REF_LOST'),('External PPS stopped','PPS_LOST'),
                        ('BDS RX timed out for 6 seconds','RX_TIMEOUT'),('RX hardware time moved backwards','TIME_BACKWARDS'),
                        ('Actual RX rate exceeds .01 sample/10ms tolerance','RATE_UNSUPPORTED'),('unexpected diagnostic 123','PROCESS_EXIT')]:
            with self.subTest(tag=tag):
                log.write_text('BDS RX rate: requested=4092000 actual=4092020\n'+raw+'\n',encoding='utf-8')
                text=str(launch_bds.process_error('USRP',['receiver'],3,log))
                self.assertIn('['+tag+']',text);self.assertIn(raw,text);self.assertIn('actual=4092020',text);self.assertIn('完整日志',text)

    def test_main_preserves_exit_code_without_dumping_python_command(self):
        log=self.root/'receiver.log';log.write_text('External PPS not detected\n',encoding='utf-8')
        error=launch_bds.process_error('USRP',['receiver','--long-command'],2,log)
        probe=MagicMock();probe.__enter__.return_value=probe;probe.connect_ex.return_value=1
        stderr=io.StringIO()
        with patch.object(sys,'argv',['launch_bds.py','--ui-only','--config',str(self.config)]),patch.object(launch_bds.socket,'socket',return_value=probe), \
             patch.object(launch_bds.signal,'signal'),patch.object(launch_bds,'run_child',side_effect=error),redirect_stderr(stderr),redirect_stdout(io.StringIO()):
            self.assertEqual(launch_bds.main(),2)
        self.assertIn('[PPS_MISSING]',stderr.getvalue());self.assertNotIn('--long-command',stderr.getvalue())

    def test_occupied_port_is_rejected_without_temporary_bind(self):
        probe=MagicMock();probe.__enter__.return_value=probe;probe.connect_ex.return_value=0
        with patch.object(sys,'argv',['launch_bds.py','--ui-only','--config',str(self.config)]), \
             patch.object(launch_bds.socket,'socket',return_value=probe), \
             patch.object(launch_bds.signal,'signal'), \
             patch.object(launch_bds,'run_child') as child:
            self.assertEqual(launch_bds.main(),1)
        probe.bind.assert_not_called();child.assert_not_called()

    def test_server_package_import_and_bds_default_page(self):
        # The 5G shell entry imports ui.server; command-line tests import server.
        sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
        try:self.assertTrue(hasattr(importlib.import_module('ui.server'),'load_settings'))
        finally:sys.path.pop(0)
        from urllib.request import urlopen
        import threading
        settings=load_settings(Path(__file__).resolve().parents[2]/'config/config.f')
        settings['default_signal']='bds'
        server=make_server(settings,MonitorStore(settings),port=0)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            with urlopen(f'http://127.0.0.1:{server.server_address[1]}/') as response:
                self.assertIn(b'data-default-signal="bds"',response.read())
        finally:server.shutdown();server.server_close();thread.join()


if __name__=='__main__':unittest.main()
