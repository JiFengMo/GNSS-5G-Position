import bisect
from contextlib import closing
import csv
import gzip
import json
from pathlib import Path
import shutil
import sqlite3
import sys
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request,urlopen

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from gnss import GNSSStore,import_run,curve,dumps,uint
from server import load_settings,MonitorStore,make_server


def write_csv(path,names,records):
    with Path(path).open("w",newline="",encoding="utf-8") as f:
        writer=csv.DictWriter(f,fieldnames=names);writer.writeheader();writer.writerows(records)


class GNSSTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base=tempfile.TemporaryDirectory();cls.root=Path(cls.base.name)
        fixture=Path(__file__).resolve().parents[2]/"src/GNSS/tests/data/b1c_position_matlab.json.gz"
        data=json.loads(gzip.decompress(fixture.read_bytes()))
        cls.tracking=cls.root/"tracking";cls.navigation=cls.root/"navigation";cls.position=cls.root/"position"
        for p in (cls.tracking,cls.navigation,cls.position):p.mkdir()
        meta=dict(sample_rate_hz=18e6,if_hz=20000,complete=True,status_code=0,period_ms=200,
                  elevation_mask_deg=5,troposphere=True,ldpc_error_correction=False,pll_bandwidth_hz=18,
                  dll_bandwidth_hz=1,early_late_offset_chips=.06,quality_epochs=50)
        for p in (cls.tracking,cls.navigation,cls.position):(p/"run.json").write_text(dumps(meta))
        cls.tracks={}
        for prn,values in data["tracking"].items():
            records=[dict(zip(data["tracking_columns"],r)) for r in values]
            cls.tracks[int(prn)]=records
            write_csv(cls.tracking/f"prn_{int(prn):02d}.csv",data["tracking_columns"],records)
        nav=[dict(zip(data["navigation_columns"],r)) for r in data["navigation_frames"]]
        write_csv(cls.navigation/"frames.csv",data["navigation_columns"],nav)
        solutions=[dict(r,valid=1,status="ok") for r in data["solutions"]]
        write_csv(cls.position/"solutions.csv",solutions[0].keys(),solutions)
        obs=[]
        for r in data["observations"]:
            prn=int(r["prn"]);track=cls.tracks[prn]
            index=bisect.bisect_right([int(t["absolute_sample"]) for t in track],int(r["absolute_sample"]))-1
            e=track[index]
            obs.append(dict(r,generation=e["generation"],epoch=e["epoch"],used=1))
        write_csv(cls.position/"observations.csv",obs[0].keys(),obs)
        (cls.position/"exclusions.csv").write_text("index,prn,reason\n1,25,below_elevation_mask\n")
        cls.experiments=cls.root/"experiments"
        cls.meta=import_run(cls.tracking,cls.experiments/"fixture",navigation=cls.navigation,positioning=cls.position,name="真实 MATLAB 小型参考")
        cls.store=GNSSStore(cls.experiments)

    @classmethod
    def tearDownClass(cls):cls.base.cleanup()

    def test_import_metadata_and_source_identity(self):
        self.assertEqual(self.meta["valid_frames"],7)
        self.assertEqual(self.meta["valid_fixes"],90)
        self.assertEqual(len(self.meta["prns"]),7)
        self.assertTrue(all(len(r["sha256"])==64 for r in self.meta["sources"]))
        self.assertEqual(self.store.recordings()[0]["run_id"],"fixture")
        with self.assertRaises(FileExistsError):import_run(self.tracking,self.experiments/"fixture")

    def test_navigation_causality_and_retrospective_position(self):
        d=self.store.snapshot("fixture",10,25)
        self.assertEqual(d["counts"]["tracked"],7)
        self.assertEqual(d["counts"]["navigation_ready"],0)
        self.assertEqual(d["counts"]["received_frames"],0)
        self.assertEqual(d["frames"],[])
        self.assertTrue(d["position"]["valid"])
        self.assertGreater(d["position"]["available_s"],d["cursor_s"])
        self.assertEqual(self.store.frame_page("fixture",10,25)["total"],0)
        self.assertEqual(self.store.frame_page("fixture",30,25)["total"],1)

    def test_snapshots_are_bounded_and_json_has_no_nonfinite_numbers(self):
        d=self.store.snapshot("fixture",20,23,120)
        json.dumps(d,allow_nan=False)
        for line in d["series"].values():self.assertLessEqual(len(line["x"]),1200)
        self.assertLessEqual(len(d["position_history"]),1604)
        self.assertIsInstance(d["satellites"][0]["tracking"]["absolute_sample"],str)
        self.assertLess(len(dumps(d)),800000)
        with self.assertRaises(ValueError):self.store.snapshot("fixture",float("nan"))
        with self.assertRaises(ValueError):self.store.snapshot("fixture",10,1)
        with self.assertRaises(ValueError):self.store.snapshot("fixture",10,23,1000)

    def test_curve_preserves_generation_boundaries_and_missing_data(self):
        records=[dict(time_s=j/100,segment=0 if j<100 else 1,value=None if 45<=j<=50 else (1000 if j==80 else j)) for j in range(200)]
        c=curve(records,"value",80)
        self.assertIn(1000,c["y"])
        self.assertIn(None,c["y"])
        at=c["x"].index(1)
        self.assertIsNone(c["y"][at])
        self.assertLessEqual(len(c["x"]),160)

    def test_partial_tracking_only_and_uint64_counter_preservation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);track=root/"tracking";track.mkdir()
            (track/"run.json").write_text(dumps(dict(sample_rate_hz=1000,complete=False)))
            sample=2**53+123
            r=dict(epoch="0",generation="0",absolute_sample=str(sample),sample_count=10,integration_s=.01,state="locked",valid=1)
            write_csv(track/"prn_23.csv",r.keys(),[r])
            m=import_run(track,root/"runs/partial")
            d=GNSSStore(root/"runs").snapshot("partial")
            self.assertEqual(m["valid_fixes"],0)
            self.assertIsNone(d["position"])
            self.assertIsNone(d["counts"]["captured"])
            self.assertEqual(d["satellites"][0]["last_tracking"]["absolute_sample"],str(sample))
            self.assertEqual(uint(r,"absolute_sample"),sample)
            self.assertFalse(d["run"]["tracking"]["complete"])

    def test_wrong_navigation_span_and_sample_rate_are_rejected_cleanly(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);nav=root/"nav";shutil.copytree(self.navigation,nav)
            with (nav/"frames.csv").open() as source:
                rows=list(csv.DictReader(source))
            rows[0]["absolute_sample"]=str(int(rows[0]["absolute_sample"])+1)
            write_csv(nav/"frames.csv",rows[0].keys(),rows)
            with self.assertRaisesRegex(ValueError,"match|mismatch"):
                import_run(self.tracking,root/"failed",navigation=nav)
            self.assertFalse((root/"failed").exists())
            (nav/"run.json").write_text(dumps(dict(sample_rate_hz=1)))
            with self.assertRaisesRegex(ValueError,"sample rates"):
                import_run(self.tracking,root/"failed-rate",navigation=nav)

    def test_read_only_store_and_path_rejection(self):
        for name in ("../fixture","fixture/data.sqlite","..","fixture%2f.."):
            with self.assertRaises(ValueError):self.store.snapshot(name)
        path,_=self.store.run("fixture")
        with closing(self.store.connect(path)) as db:
            with self.assertRaises(sqlite3.OperationalError):db.execute("DELETE FROM positions")
        with self.assertRaises(ValueError):self.store.frame_page("fixture",10,23,-1)

    def test_http_endpoints_and_invalid_queries(self):
        settings=load_settings(Path(__file__).resolve().parents[2]/"config/config.f")
        settings.update(directory=self.root/"no-5g",gnss_directory=self.experiments)
        server=make_server(settings,MonitorStore(settings),port=0)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        base=f"http://127.0.0.1:{server.server_address[1]}"
        try:
            for route in ("/gnss.js","/gnss.css","/api/gnss/runs","/api/gnss/state?run=fixture&time=10&prn=25","/api/gnss/frames?run=fixture&time=25&prn=25"):
                with urlopen(base+route) as response:self.assertEqual(response.status,200)
            for query in ("run=../fixture","run=fixture&time=nan","run=fixture&window=inf","run=fixture&prn=0"):
                with self.assertRaises(HTTPError):urlopen(base+"/api/gnss/state?"+query)
            with self.assertRaises(HTTPError) as err:urlopen(Request(base+"/api/gnss/runs",data=b"{}",method="POST"))
            self.assertEqual(err.exception.code,501)
        finally:server.shutdown();server.server_close();thread.join()


if __name__=="__main__":unittest.main()
