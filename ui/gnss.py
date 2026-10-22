"""Indexed, read-only B1C result adapter. No IQ processing or hardware control."""
from __future__ import annotations
import bisect
from contextlib import closing
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import sqlite3

IDENTIFIER = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}\Z")
COUNTERS = {"absolute_sample", "generation", "epoch", "first_epoch", "end_sample", "record_index",
            "first_sample", "last_sample_end", "input_start_sample", "code_phase_samples", "hardware_sample"}
MAX_ROWS = 10_000_000
SCHEMA = """
CREATE TABLE tracking(prn INTEGER,t REAL,end_t REAL,generation TEXT,epoch TEXT,segment INTEGER,valid INTEGER,payload TEXT);
CREATE INDEX track_time ON tracking(prn,t);
CREATE UNIQUE INDEX track_epoch ON tracking(prn,generation,epoch);
CREATE TABLE frames(prn INTEGER,t REAL,available REAL,generation TEXT,epoch TEXT,segment INTEGER,valid INTEGER,payload TEXT);
CREATE INDEX frame_time ON frames(prn,available);
CREATE TABLE acquisitions(prn INTEGER PRIMARY KEY,payload TEXT);
CREATE TABLE positions(idx INTEGER PRIMARY KEY,t REAL,available REAL,valid INTEGER,payload TEXT);
CREATE INDEX position_time ON positions(t);
CREATE TABLE observations(idx INTEGER,prn INTEGER,payload TEXT);
CREATE UNIQUE INDEX obs_key ON observations(idx,prn);
CREATE TABLE exclusions(idx INTEGER,prn INTEGER,payload TEXT);
CREATE TABLE events(prn INTEGER,t REAL,kind TEXT,payload TEXT);
CREATE INDEX event_time ON events(prn,t);
"""


def dumps(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"), parse_constant=lambda _: None)


def uint(row, key):
    value = str(row[key])
    if not value.isascii() or not value.isdigit() or int(value) > 2**64 - 1:
        raise ValueError(f"Invalid unsigned counter {key}: {value}")
    return int(value)


def numeric(value):
    if value == "":
        return None
    try:
        if re.fullmatch(r"[+-]?\d+",value): return int(value)
        result = float(value)
        return result if math.isfinite(result) else None
    except ValueError:
        return value


def rows(path, required=()):
    with Path(path).open(encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        names = reader.fieldnames or []
        if len(names) != len(set(names)) or not set(required).issubset(names):
            raise ValueError(f"Missing or duplicate columns: {path}")
        for count, raw in enumerate(reader, 1):
            if count > MAX_ROWS or None in raw or any(v is None for v in raw.values()):
                raise ValueError(f"Malformed/oversized CSV: {path}:{reader.line_num}")
            if sum(map(len, raw.values())) > 128 * 1024:
                raise ValueError(f"Oversized CSV row: {path}")
            yield {k: v if k in COUNTERS or k in ("bits", "state", "status", "event", "reason") else numeric(v)
                   for k, v in raw.items()}


def finite(value):
    return isinstance(value, (int, float)) and math.isfinite(value)


def payload(row):
    return json.loads(row[0]) if row else None


def signature(path):
    p = Path(path)
    st = p.stat()
    h = hashlib.sha256()
    with p.open("rb") as f:
        for data in iter(lambda: f.read(1024 * 1024), b""):
            h.update(data)
    return dict(path=str(p.resolve()), bytes=st.st_size, mtime_ns=st.st_mtime_ns, sha256=h.hexdigest())


def import_run(tracking, output, *, navigation=None, positioning=None, acquisition=None, name=None):
    """Create one immutable experiment, atomically published by run.json last."""
    tracking, output = Path(tracking), Path(output)
    if not IDENTIFIER.fullmatch(output.name):
        raise ValueError("Output folder name must contain only letters, digits, '-' or '_'")
    config = load_json(tracking / "run.json")
    fs = config.get("sample_rate_hz")
    if not finite(fs) or fs <= 0:
        raise ValueError("Tracking run.json has no valid sample_rate_hz")
    if positioning and not navigation:
        raise ValueError("Positioning requires the matching navigation directory")
    sources = {}
    def remember(p):
        p = Path(p); sources[str(p.resolve())] = signature(p); return p
    remember(tracking / "run.json")
    output.mkdir(parents=True, exist_ok=False)
    db = None
    try:
        db = sqlite3.connect(output / "data.sqlite")
        db.executescript(SCHEMA)
        prns, start, end, counts, origin = [], math.inf, 0.0, {}, None
        for p in sorted(tracking.glob("prn_*.csv")):
            match = re.fullmatch(r"prn_(\d{2})\.csv", p.name)
            if not match or not 1 <= int(match[1]) <= 63:
                raise ValueError(f"Invalid PRN filename: {p}")
            prn = int(match[1]); remember(p)
            previous = None; segment = 0; count = 0
            required = ("epoch", "generation", "absolute_sample", "sample_count", "integration_s", "state", "valid")
            for r in rows(p, required):
                sample, n, epoch, generation = (uint(r, k) for k in ("absolute_sample", "sample_count", "epoch", "generation"))
                if not n or sample + n > 2**64 - 1 or r["valid"] not in (0, 1) or r["state"] not in ("pull_in", "locked", "lost"):
                    raise ValueError(f"Invalid tracking epoch: {p}")
                if r["valid"] and r["state"] != "locked":
                    raise ValueError("A valid tracking epoch must be locked")
                if not finite(r["integration_s"]) or abs(n / fs - r["integration_s"]) > 1e-9:
                    raise ValueError("Sample rate / integration time mismatch")
                boundary = None
                if previous:
                    ps, pn, pe, pg, pv, state = previous
                    if sample < ps + pn or generation < pg or (generation == pg and epoch <= pe):
                        raise ValueError(f"Out-of-order tracking epochs: {p}")
                    if generation != pg or sample != ps + pn or epoch != pe + 1 or not pv or not r["valid"]:
                        segment += 1
                    if generation != pg: boundary = "generation_reset"
                    elif sample != ps + pn or epoch != pe + 1: boundary = "sample_gap"
                    elif r["state"] != state: boundary = r["state"]
                else:
                    boundary = r["state"]
                t = sample / fs
                r.update(prn=prn, time_s=t, end_time_s=(sample+n)/fs, segment=segment)
                db.execute("INSERT INTO tracking VALUES(?,?,?,?,?,?,?,?)", (prn,t,r["end_time_s"],str(generation),str(epoch),segment,int(r["valid"]),dumps(r)))
                if boundary:
                    e = dict(prn=prn,time_s=t,event=boundary,generation=str(generation),absolute_sample=str(sample))
                    db.execute("INSERT INTO events VALUES(?,?,?,?)", (prn,t,boundary,dumps(e)))
                previous = sample,n,epoch,generation,r["valid"],r["state"]
                start = min(start,t); end = max(end,r["end_time_s"]); count += 1
            if not count:
                raise ValueError(f"Empty tracking CSV: {p}")
            prns.append(prn); counts[prn] = count
        if not prns:
            raise ValueError("No prn_XX.csv tracking files found")
        if acquisition:
            for r in rows(remember(acquisition), ("prn", "acquired")):
                prn = uint(r,"prn")
                if not 1 <= prn <= 63 or r["acquired"] not in (0,1):
                    raise ValueError("Invalid acquisition result")
                db.execute("INSERT INTO acquisitions VALUES(?,?)", (prn,dumps(r)))
        # Capture run events include reacquisition attempts and state changes.
        if (tracking / "events.csv").is_file():
            for r in rows(remember(tracking / "events.csv"), ("prn","absolute_sample","event")):
                prn = uint(r,"prn"); t = uint(r,"absolute_sample")/fs
                r.update(time_s=t)
                db.execute("INSERT INTO events VALUES(?,?,?,?)", (prn,t,r["event"],dumps(r)))
        nav_config = None; frame_count = 0
        if navigation:
            navigation = Path(navigation); nav_config = load_json(remember(navigation / "run.json"))
            if nav_config.get("sample_rate_hz") != fs:
                raise ValueError("Navigation/tracking sample rates differ")
            ephemerides = {}
            eph_path = navigation / "ephemerides.csv"
            if eph_path.exists():
                for e in rows(remember(eph_path), ("prn","generation","first_epoch","absolute_sample","valid")):
                    ephemerides[uint(e,"prn"),str(e["generation"]),str(e["first_epoch"])] = e
            for r in rows(remember(navigation / "frames.csv"), ("prn","generation","first_epoch","absolute_sample","end_sample","valid","status","crc2_ok","crc3_ok","bits")):
                prn, sample, stop, first = (uint(r,k) for k in ("prn","absolute_sample","end_sample","first_epoch"))
                generation = str(r["generation"]); t = sample/fs; available = stop/fs
                if prn not in prns or stop <= sample or r["valid"] not in (0,1):
                    raise ValueError("Invalid navigation frame")
                span = db.execute("SELECT payload,segment FROM tracking WHERE prn=? AND generation=? AND t>=? AND t<? ORDER BY t LIMIT 1801", (prn,generation,t,available)).fetchall()
                if len(span) != 1800:
                    raise ValueError("Navigation frame does not match 1800 tracking epochs")
                values = [json.loads(s[0]) for s in span]
                if uint(values[0],"absolute_sample") != sample or uint(values[-1],"absolute_sample")+uint(values[-1],"sample_count") != stop:
                    raise ValueError("Navigation frame sample span mismatch")
                segment = span[0][1]
                if r["valid"]:
                    if r["status"] != "ok" or r["crc2_ok"] != 1 or r["crc3_ok"] != 1 or len(r["bits"]) != 878 or set(r["bits"]) - {"0","1"}:
                        raise ValueError("Inconsistent valid navigation frame")
                    if any(s[1] != segment or not v["valid"] or uint(v,"epoch") != first+j for j,(s,v) in enumerate(zip(span,values))):
                        raise ValueError("Navigation crosses a tracking gap/reset")
                    e = ephemerides.get((prn,generation,str(first)))
                    if e is not None and (not e["valid"] or uint(e,"absolute_sample") != sample):
                        raise ValueError("Ephemeris/frame mismatch")
                    r["ephemeris"] = e
                    frame_count += 1
                r.update(time_s=t,available_s=available,segment=segment)
                db.execute("INSERT INTO frames VALUES(?,?,?,?,?,?,?,?)", (prn,t,available,generation,str(first),segment,int(r["valid"]),dumps(r)))
        position_config = None; fixes = 0; position_count = 0; default_time = None
        if positioning:
            positioning = Path(positioning); position_config = load_json(remember(positioning / "run.json"))
            if position_config.get("sample_rate_hz") != fs:
                raise ValueError("Positioning/tracking sample rates differ")
            for r in rows(remember(positioning / "solutions.csv"), ("index","absolute_sample","valid","status","satellites")):
                index, sample = uint(r,"index"),uint(r,"absolute_sample"); t = sample/fs
                if r["valid"] not in (0,1) or not start <= t <= end:
                    raise ValueError("Position epoch outside tracking recording")
                if r["valid"] and (r["status"] != "ok" or r["satellites"] < 4 or not all(finite(r.get(k)) for k in ("x_m","y_m","z_m","latitude_deg","longitude_deg","height_m"))):
                    raise ValueError("Invalid positioning result")
                r.update(time_s=t,available_s=None)
                if r["valid"] and origin is None:
                    origin = {k:r[k] for k in ("x_m","y_m","z_m","latitude_deg","longitude_deg","height_m")}
                db.execute("INSERT INTO positions VALUES(?,?,?,?,?)", (index,t,None,int(r["valid"]),dumps(r)))
                fixes += int(r["valid"]); position_count += 1; default_time = t
            for r in rows(remember(positioning / "observations.csv"), ("index","prn","absolute_sample","generation","epoch","used")):
                index,prn,sample = (uint(r,k) for k in ("index","prn","absolute_sample"))
                pos = db.execute("SELECT t,valid,payload FROM positions WHERE idx=?", (index,)).fetchone()
                track = db.execute("SELECT payload,segment FROM tracking WHERE prn=? AND generation=? AND epoch=?", (prn,str(r["generation"]),str(r["epoch"]))).fetchone()
                if not pos or not track or abs(pos[0]-sample/fs)>1e-9:
                    raise ValueError("Observation does not match its position/tracking epoch")
                e = json.loads(track[0]); anchor = db.execute("SELECT available FROM frames WHERE prn=? AND generation=? AND segment=? AND t<=? AND valid=1 ORDER BY t DESC LIMIT 1", (prn,str(r["generation"]),track[1],sample/fs)).fetchone()
                if r["used"] and (not e["valid"] or not uint(e,"absolute_sample") <= sample < uint(e,"absolute_sample")+uint(e,"sample_count") or not anchor or not pos[1]):
                    raise ValueError("Used observation lacks a valid continuous frame/track")
                db.execute("INSERT INTO observations VALUES(?,?,?)", (index,prn,dumps(r)))
                if r["used"]:
                    db.execute("UPDATE positions SET available=MAX(COALESCE(available,0),?) WHERE idx=?", (anchor[0],index))
            for index,t,available,valid,text in db.execute("SELECT * FROM positions").fetchall():
                r = json.loads(text)
                used = db.execute("SELECT payload FROM observations WHERE idx=?", (index,)).fetchall()
                actual_used = sum(bool(json.loads(v[0])["used"]) for v in used)
                if valid and (actual_used != int(r["satellites"]) or not available):
                    raise ValueError("Position satellite count differs from used observations")
                r["available_s"] = available
                db.execute("UPDATE positions SET payload=? WHERE idx=?", (dumps(r),index))
            exclusion_path = positioning / "exclusions.csv"
            if exclusion_path.exists():
                for r in rows(remember(exclusion_path), ("index","prn","reason")):
                    db.execute("INSERT INTO exclusions VALUES(?,?,?)", (uint(r,"index"),uint(r,"prn"),dumps(r)))
        db.commit(); db.close(); db = None
        for value in sources.values():
            st = Path(value["path"]).stat()
            if st.st_size != value["bytes"] or st.st_mtime_ns != value["mtime_ns"]:
                raise ValueError("Input changed during import; import after processing finishes")
        metadata = dict(schema=1,kind="b1c_offline",run_id=output.name,name=name or output.name,
                        imported_utc=datetime.now(timezone.utc).isoformat(),sample_rate_hz=fs,
                        start_s=min(0,start),end_s=end,default_time_s=default_time if default_time is not None else max(start,end-.02),
                        prns=prns,tracking_epochs=sum(counts.values()),valid_frames=frame_count,
                        position_epochs=position_count,valid_fixes=fixes,origin=origin,
                        tracking=config,navigation=nav_config,positioning=position_config,
                        sources=list(sources.values()),offline_retrospective=True)
        (output / "run.json").write_text(dumps(metadata)+"\n", encoding="utf-8")
        return metadata
    except Exception:
        if db is not None: db.close()
        # Only files in the fresh directory created by this invocation.
        for leaf in ("data.sqlite","data.sqlite-journal","data.sqlite-wal","data.sqlite-shm","run.json"):
            (output / leaf).unlink(missing_ok=True)
        output.rmdir()
        raise


def curve(records, field, limit=600):
    """Min/max buckets retain original times, missing values and reset breaks."""
    if not records: return dict(x=[],y=[])
    selected = {0,len(records)-1}
    bucket = max(1,math.ceil(len(records)/(limit/4)))
    for first in range(0,len(records),bucket):
        indices = [j for j in range(first,min(first+bucket,len(records))) if finite(records[j].get(field))]
        if indices:
            selected.add(min(indices,key=lambda j:records[j][field])); selected.add(max(indices,key=lambda j:records[j][field]))
    # Preserve boundaries before thinning. Each has a null separator.
    boundaries = {j for j in range(1,len(records)) if records[j]["segment"] != records[j-1]["segment"] or
                  finite(records[j].get(field)) != finite(records[j-1].get(field))}
    # Highly fragmented recordings have a bounded display window, never join
    # omitted fragments. Keep the latest part and tell clients the input size.
    low = max(0,len(records)-12000)
    chosen = sorted((selected | boundaries | {max(0,j-1) for j in boundaries}) & set(range(low,len(records))))
    if len(chosen)>limit:
        chosen=chosen[-limit:]
    x,y=[],[]; previous=None
    for j in chosen:
        r=records[j]
        if previous is not None and (r["segment"] != records[previous]["segment"] or any(previous<k<=j for k in boundaries)):
            x.append(r["time_s"]); y.append(None)
        x.append(r["time_s"]); y.append(r.get(field) if finite(r.get(field)) else None); previous=j
    return dict(x=x,y=y,original_points=len(records))


class GNSSStore:
    def __init__(self,directory):
        self.directory=Path(directory)

    def run(self,identifier):
        if not IDENTIFIER.fullmatch(identifier): raise ValueError("Choose a GNSS experiment from the list")
        root=self.directory.resolve(); folder=self.directory/identifier
        if folder.is_symlink() or folder.resolve().parent != root:
            raise ValueError("GNSS recording is unavailable")
        for leaf in ("run.json","data.sqlite"):
            if (folder/leaf).is_symlink() or not (folder/leaf).is_file():
                raise ValueError("GNSS recording is unavailable")
        meta=load_json(folder/"run.json")
        if meta.get("schema") != 1 or meta.get("kind") != "b1c_offline" or meta.get("run_id") != identifier:
            raise ValueError("Unsupported GNSS recording")
        return folder/"data.sqlite",meta

    def recordings(self):
        result=[]
        if not self.directory.exists(): return result
        for p in self.directory.iterdir():
            if not p.is_dir() or p.is_symlink(): continue
            try:
                _,m=self.run(p.name)
                result.append({k:m[k] for k in ("run_id","name","imported_utc","start_s","end_s","default_time_s","prns","valid_frames","valid_fixes")})
            except (ValueError,KeyError,OSError,json.JSONDecodeError): continue
        return sorted(result,key=lambda r:r["imported_utc"],reverse=True)

    @staticmethod
    def connect(path):
        return sqlite3.connect(path.resolve().as_uri()+"?mode=ro",uri=True,timeout=2)

    def snapshot(self,identifier,time_s=None,prn=None,window=30):
        path,meta=self.run(identifier)
        t=meta["default_time_s"] if time_s is None else float(time_s)
        if not finite(t) or not finite(window) or window<=0 or window>120:
            raise ValueError("Finite time and window 0..120 seconds required")
        t=max(meta["start_s"],min(meta["end_s"],t))
        prn=int(prn) if prn is not None else meta["prns"][0]
        if prn not in meta["prns"]: raise ValueError("PRN is not present in this recording")
        with closing(self.connect(path)) as db:
            sats=[]
            for p in meta["prns"]:
                latest=payload(db.execute("SELECT payload FROM tracking WHERE prn=? AND t<=? ORDER BY t DESC LIMIT 1",(p,t)).fetchone())
                current=latest if latest and t<latest["end_time_s"] else None
                frame=payload(db.execute("SELECT payload FROM frames WHERE prn=? AND available<=? AND valid=1 ORDER BY available DESC LIMIT 1",(p,t)).fetchone())
                # A frame from a lost/reacquired segment cannot make the new
                # tracking segment navigation-ready.
                ready=bool(frame and current and frame["segment"]==current["segment"] and current["valid"])
                sats.append(dict(prn=p,tracking=current,last_tracking=latest,navigation_ready=ready,
                                 frame=frame,acquisition=payload(db.execute("SELECT payload FROM acquisitions WHERE prn=?",(p,)).fetchone())))
            position=payload(db.execute("SELECT payload FROM positions WHERE t<=? ORDER BY t DESC LIMIT 1",(t,)).fetchone())
            period=(meta.get("positioning") or {}).get("period_ms",200)/1000
            position_current=bool(position and t-position["time_s"]<=period*1.05)
            observations=[]; exclusions=[]
            if position and position_current:
                observations=[json.loads(r[0]) for r in db.execute("SELECT payload FROM observations WHERE idx=? ORDER BY prn",(int(position["index"]),))]
                exclusions=[json.loads(r[0]) for r in db.execute("SELECT payload FROM exclusions WHERE idx=? ORDER BY prn",(int(position["index"]),))]
            reasons={int(r["prn"]):r["reason"] for r in exclusions}
            by_prn={int(r["prn"]):r for r in observations}
            for s in sats:
                obs=by_prn.get(s["prn"])
                s.update(observation=obs,used=bool(position_current and position["valid"] and obs and obs["used"]),
                         exclusion=reasons.get(s["prn"]))
            records=[json.loads(r[0]) for r in db.execute("SELECT payload FROM tracking WHERE prn=? AND t>=? AND t<=? ORDER BY t LIMIT 12001",(prn,max(meta["start_s"],t-window),t))]
            fields=("doppler_hz","cn0_db_hz","dll_error","pll_error","data_i_p","data_q_p","pilot_i_p","pilot_q_p","residual_frequency_hz")
            series={k:curve(records,k) for k in fields}
            events=[json.loads(r[0]) for r in db.execute("SELECT payload FROM events WHERE prn=? AND t<=? ORDER BY t DESC LIMIT 60",(prn,t))][::-1]
            n=db.execute("SELECT COUNT(*) FROM positions WHERE t<=?",(t,)).fetchone()[0]
            stride=max(1,math.ceil(n/800))
            history=[]
            for text,lowest,begin in db.execute("SELECT p.payload,b.lowest,b.begin FROM (SELECT MAX(idx) last,MIN(valid) lowest,MIN(t) begin FROM positions WHERE t<=? GROUP BY idx/?) b JOIN positions p ON p.idx=b.last ORDER BY p.t LIMIT 802",(t,stride)):
                item=json.loads(text)
                if not lowest and item["valid"]:
                    history.append(dict(time_s=begin,valid=0))
                history.append(item)
            frames=[json.loads(r[0]) for r in db.execute("SELECT payload FROM frames WHERE prn=? AND available<=? ORDER BY available DESC LIMIT 20",(prn,t))]
            acquisitions=[json.loads(r[0]) for r in db.execute("SELECT payload FROM acquisitions")]
            counts=dict(captured=sum(bool(r.get("acquired")) for r in acquisitions) if acquisitions else None,
                        capture_attempts=len(acquisitions),
                        tracked=sum(bool(s["tracking"] and s["tracking"]["valid"]) for s in sats),
                        navigation_ready=sum(s["navigation_ready"] for s in sats),used=sum(s["used"] for s in sats),
                        received_frames=db.execute("SELECT COUNT(*) FROM frames WHERE valid=1 AND available<=?",(t,)).fetchone()[0])
        # Keep full source paths/hashes in a collapsed metadata detail, not in
        # the normal satellite/position workflow.
        return dict(run=meta,cursor_s=t,selected_prn=prn,satellites=sats,counts=counts,
                    position=position,position_current=position_current,observations=observations,
                    exclusions=exclusions,series=series,events=events,position_history=history,frames=frames)

    def frame_page(self,identifier,time_s,prn,offset=0,limit=20):
        path,meta=self.run(identifier)
        if not finite(time_s) or prn not in meta["prns"] or offset<0 or offset>MAX_ROWS or not 1<=limit<=50:
            raise ValueError("Invalid frame page")
        with closing(self.connect(path)) as db:
            total=db.execute("SELECT COUNT(*) FROM frames WHERE prn=? AND available<=?",(prn,time_s)).fetchone()[0]
            records=[json.loads(r[0]) for r in db.execute("SELECT payload FROM frames WHERE prn=? AND available<=? ORDER BY available DESC LIMIT ? OFFSET ?",(prn,time_s,limit,offset))]
        return dict(frames=records,total=total,offset=offset,limit=limit)
