import copy
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from server import MonitorStore, load_settings, make_server, replay_page, decode_line, DEFAULTS
from make_demo import generate


class MonitorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "config/UI").mkdir(parents=True)
        (self.root / "config/config.f").write_text("enable_5g=1\nnr_config=receiver.f\nui_config=UI/monitor.f\n")
        (self.root / "config/receiver.f").write_text("sample_rate_hz=30720000\n")
        (self.root / "config/UI/monitor.f").write_text("record_dir=monitor\nmax_history=100\n")
        self.settings=load_settings(self.root / "config/config.f", self.root)
        self.directory=self.settings["directory"]
        self.path=generate(self.directory, 80)
        self.lines=self.path.read_bytes().splitlines(keepends=True)
        self.config=json.loads(self.lines[0])

    def tearDown(self):
        self.temp.cleanup()

    def real_recording(self, lines=None):
        meta=copy.deepcopy(self.config);meta.pop("demo");meta["run_id"]="run-test";meta["time_ms"]=time.time()*1000
        selected=lines or self.lines[1:]
        rows=[json.dumps(meta).encode()+b"\n"]
        for line in selected:
            row=json.loads(line);row["run_id"]="run-test";row["time_ms"]=meta["time_ms"];rows.append(json.dumps(row).encode()+b"\n")
        path=self.directory/"run-test-000.jsonl";path.write_bytes(b"".join(rows));return path

    def test_replay_pages_preserve_event_ids_and_precise_sample_counters(self):
        first=replay_page(self.directory,self.path.name,0,17)
        second=replay_page(self.directory,self.path.name,first["next_offset"],17)
        self.assertEqual(first["events"][-1]["seq"]+1,second["events"][0]["seq"])
        sample=next(e for e in first["events"] if e["kind"]=="sample")
        self.assertEqual(sample["hardware_sample"],"10000000000000000")
        self.assertTrue(first["config"]["demo"])

    def test_partial_last_line_waits_until_complete(self):
        self.path.write_bytes(self.lines[0]+self.lines[1][:-1])
        page=replay_page(self.directory,self.path.name,0)
        self.assertEqual(page["events"],[])
        self.assertFalse(page["eof"])
        with self.path.open("ab") as fp:fp.write(b"\n")
        self.assertEqual(len(replay_page(self.directory,self.path.name,page["next_offset"])["events"]),1)

    def test_demo_never_becomes_live_and_history_is_bounded(self):
        store=MonitorStore(self.settings);store.poll()
        self.assertIsNone(store.snapshot()["run_id"])
        self.real_recording();store.poll();data=store.snapshot()
        self.assertEqual(data["run_id"],"run-test")
        self.assertTrue(data["connected"])
        self.assertEqual(len(data["cells"]),2)
        self.assertLessEqual(len(store.history),100)
        self.assertTrue(data["trace"]["curves"])

    def test_bad_lines_and_bad_curve_schema_do_not_break_reader(self):
        path=self.real_recording(self.lines[1:4])
        with path.open("ab") as fp:fp.write(b"bad json\n")
        store=MonitorStore(self.settings);store.poll()
        self.assertEqual(store.invalid_lines,1)
        bad=json.loads(self.lines[2]);bad["curves"]={"iq_i":{"x":[1],"y":[]}}
        self.assertIsNone(decode_line(json.dumps(bad).encode()))

    def test_file_rotation_resets_tail_and_rejects_old_replay_cursor(self):
        path=self.real_recording(self.lines[1:4]);store=MonitorStore(self.settings);store.poll()
        page=replay_page(self.directory,path.name,0)
        rows=path.read_text().splitlines();meta=json.loads(rows[0]);meta["time_ms"]+=1000
        path.write_text(json.dumps(meta)+"\n"+rows[1]+"\n")
        store.poll()
        with self.assertRaisesRegex(ValueError,"rotated"):
            replay_page(self.directory,path.name,page["next_offset"],generation=page["config"]["time_ms"])

    def test_paths_and_unknown_settings_are_rejected(self):
        with self.assertRaises(ValueError):replay_page(self.directory,"../outside.jsonl",0)
        (self.root/"config/UI/monitor.f").write_text("record_dir=../outside\n")
        with self.assertRaises(ValueError):load_settings(self.root/"config/config.f",self.root)
        (self.root/"config/UI/monitor.f").write_text("wrong_option=1\n")
        with self.assertRaises(ValueError):load_settings(self.root/"config/config.f",self.root)

    def test_http_is_read_only_and_serves_pages(self):
        store=MonitorStore(self.settings);self.real_recording();store.poll()
        server=make_server(self.settings,store,port=0)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        base=f"http://127.0.0.1:{server.server_address[1]}"
        try:
            for path in ("/","/style.css","/app.js","/api/bootstrap","/api/state","/api/recordings"):
                with urlopen(base+path) as response:self.assertEqual(response.status,200)
            with urlopen(base+"/api/state") as response:
                self.assertFalse(json.load(response)["capabilities"]["position_solver"])
            with self.assertRaises(HTTPError) as result:urlopen(Request(base+"/api/config",data=b"{}",method="POST"))
            self.assertEqual(result.exception.code,501)
        finally:
            server.shutdown();server.server_close();thread.join()


if __name__ == "__main__":
    unittest.main()
