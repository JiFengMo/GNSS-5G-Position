import importlib
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

    def test_interruption_stops_current_child(self):
        child=Mock();child.wait.side_effect=[KeyboardInterrupt,0]
        with patch.object(launch_bds.subprocess,'Popen',return_value=child),self.assertRaises(KeyboardInterrupt):
            launch_bds.run_child(['example'])
        child.terminate.assert_called_once();self.assertEqual(child.wait.call_count,2)

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
