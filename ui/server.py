#!/usr/bin/env python3
"""Local, read-only 5G monitor and B1C offline result viewer. Standard library only."""
from __future__ import annotations
import argparse
from collections import OrderedDict, deque
import json
import math
import sqlite3
from pathlib import Path
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs
if __package__:
    from .gnss import GNSSStore
else:
    from gnss import GNSSStore

ROOT = Path(__file__).resolve().parents[1]
MAX_LINE = 256 * 1024
READ_BUDGET = 4 * 1024 * 1024
DEFAULTS = dict(enabled="1", port="8765", poll_interval_ms="200", status_interval_ms="200",
                trace_interval_ms="1000", record_dir="monitor", segment_mb="128", max_segments="8",
                max_history="5000", stale_after_ms="5000", source_mode="controlled", time_model="shared",
                absolute_toa_calibrated="0",gnss_record_dir="gnss-ui")


def read_config(path: Path) -> dict[str, str]:
    result = {}
    for no, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{no}: expected key = value")
        key, value = (p.strip() for p in line.split("=", 1))
        if not value or key in result:
            raise ValueError(f"{path}:{no}: empty or duplicate item")
        result[key] = value
    return result


def load_settings(main_path: Path, root: Path = ROOT) -> dict:
    main_path = main_path.resolve()
    main = read_config(main_path)
    ui_path = (main_path.parent / main.get("ui_config", "UI/monitor.f")).resolve()
    settings = DEFAULTS | read_config(ui_path)
    unknown = set(settings) - set(DEFAULTS)
    if unknown:
        raise ValueError(f"Unknown UI config options: {sorted(unknown)}")
    ranges = {"enabled": (0, 1), "port": (1024, 65535), "poll_interval_ms": (100, 5000),
              "status_interval_ms": (100, 60000), "trace_interval_ms": (500, 60000),
              "segment_mb": (1, 512), "max_segments": (2, 64), "max_history": (100, 50000),
              "stale_after_ms": (1000, 120000), "absolute_toa_calibrated": (0, 1)}
    for key, (low, high) in ranges.items():
        settings[key] = int(settings[key])
        if not low <= settings[key] <= high:
            raise ValueError(f"Invalid {key}: expected {low}..{high}")
    if settings["source_mode"] not in ("controlled", "commercial") or settings["time_model"] not in ("shared", "receiver_bias"):
        raise ValueError("Invalid signal source or time model")
    log_root = (root / "log").resolve()
    requested = Path(settings["record_dir"])
    if ".." in requested.parts:
        raise ValueError("record_dir cannot contain '..'")
    directory = (log_root / requested).resolve()
    if not directory.is_relative_to(log_root) or directory == log_root:
        raise ValueError("record_dir must be inside log/")
    gnss_requested=Path(settings["gnss_record_dir"])
    gnss_directory=(log_root/gnss_requested).resolve()
    if ".." in gnss_requested.parts or not gnss_directory.is_relative_to(log_root) or gnss_directory==log_root:
        raise ValueError("gnss_record_dir must be inside log/")
    child = (main_path.parent / main["nr_config"]).resolve()
    settings.update(directory=directory,gnss_directory=gnss_directory,main_path=main_path, ui_path=ui_path,
                    disk_config={"main_path": str(main_path), "nr_path": str(child), "ui_path": str(ui_path),
                                 "main": main, "nr": read_config(child)})
    return settings


def cell_key(event: dict) -> str:
    return f'{event.get("freq_hz", 0):.0f}:{event.get("pci", -1)}:{event.get("ssb", -1)}'


def valid_event(value) -> bool:
    if not isinstance(value, dict) or value.get("schema") != 1 or value.get("kind") not in ("config", "runtime", "sample"):
        return False
    if not isinstance(value.get("run_id"), str) or not isinstance(value.get("time_ms"), (int, float)):
        return False
    if not math.isfinite(value["time_ms"]):
        return False
    if value["kind"] != "config" and not isinstance(value.get("seq"), int):
        return False
    if value["kind"] == "sample":
        if not all(isinstance(value.get(k), (int, float)) and math.isfinite(value[k]) for k in ("freq_hz", "pci", "ssb")) or not isinstance(value.get("curves", {}), dict):
            return False
        if len(value.get("curves", {})) > 16:
            return False
        for curve in value.get("curves", {}).values():
            if not isinstance(curve, dict) or not isinstance(curve.get("x"), list) or not isinstance(curve.get("y"), list):
                return False
            if len(curve["x"]) != len(curve["y"]) or len(curve["x"]) > 2048:
                return False
            if not all(v is None or isinstance(v, (int, float)) and math.isfinite(v) for v in curve["x"] + curve["y"]):
                return False
    return True


def decode_line(line: bytes):
    try:
        value = json.loads(line, parse_constant=lambda _: None)
        return value if valid_event(value) else None
    except (ValueError, TypeError):
        return None


def list_recordings(directory: Path) -> list[dict]:
    rows = []
    if not directory.exists():
        return rows
    for path in directory.glob("*.jsonl"):
        if not path.is_file() or path.is_symlink():
            continue
        try:
            with path.open("rb") as fp:
                first = fp.readline(MAX_LINE)
            config = decode_line(first)
            if not config or config["kind"] != "config":
                continue
            stat = path.stat()
            rows.append(dict(file=path.name, size=stat.st_size, modified_ms=stat.st_mtime * 1000,
                             run_id=config["run_id"], started_ms=config["time_ms"], config=config))
        except OSError:
            continue
    return sorted(rows, key=lambda row: row["modified_ms"], reverse=True)


def recording_path(directory: Path, name: str) -> Path:
    if Path(name).name != name or not name.endswith(".jsonl") or "/" in name or "\\" in name:
        raise ValueError("Choose a recording from this monitor's recording list")
    path = (directory / name).resolve()
    if path.parent != directory.resolve() or path.is_symlink() or not path.is_file():
        raise ValueError("Recording is unavailable")
    return path


def replay_page(directory: Path, name: str, offset: int, limit: int = 100, generation: float | None = None) -> dict:
    path = recording_path(directory, name)
    events, invalid = [], 0
    with path.open("rb") as fp:
        header = fp.readline(MAX_LINE)
        config = decode_line(header)
        if not config or config["kind"] != "config":
            raise ValueError("Unsupported recording header")
        if generation is not None and config["time_ms"] != generation:
            raise ValueError("Recording was rotated; select it again")
        start = max(len(header), offset)
        size = path.stat().st_size
        if start > size:
            raise ValueError("Recording was rotated; select it again")
        fp.seek(start)
        # All cursors emitted by this API are complete JSONL line boundaries.
        while len(events) < max(1, min(limit, 200)) and fp.tell() - start < READ_BUDGET:
            before = fp.tell()
            line = fp.readline(MAX_LINE + 1)
            if not line:
                break
            if len(line) > MAX_LINE:
                invalid += 1
                while line and not line.endswith(b"\n") and fp.tell() - start < READ_BUDGET:
                    line = fp.readline(MAX_LINE)
                continue
            if not line.endswith(b"\n"):
                fp.seek(before)
                break
            event = decode_line(line) if len(line) <= MAX_LINE else None
            if event and event.get("run_id") == config["run_id"]:
                events.append(event)
            else:
                invalid += 1
        cursor = fp.tell()
    return dict(config=config, events=events, offset=start, next_offset=cursor,
                eof=cursor >= size, invalid_lines=invalid, file=name, size=size)


class MonitorStore:
    def __init__(self, settings: dict):
        self.settings = settings
        self.directory = settings["directory"]
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.run = None
        self.config = None
        self.runtime = None
        self.cells = OrderedDict()
        self.traces = {}
        self.history = deque(maxlen=settings["max_history"])
        self.positions = {}
        self.seen = OrderedDict()
        self.invalid_lines = 0
        self.skipped_bytes = 0
        self.last_received_ms = 0
        self.error = ""
        self.thread = None

    def ingest(self, event: dict):
        if not valid_event(event):
            self.invalid_lines += 1
            return
        if event["kind"] == "config":
            self.config = event
            return
        key = (event["run_id"], event.get("seq"))
        if key in self.seen:
            return
        self.seen[key] = True
        while len(self.seen) > self.settings["max_history"] * 2:
            self.seen.popitem(last=False)
        self.last_received_ms = max(self.last_received_ms, event["time_ms"])
        if event["kind"] == "runtime":
            if not self.runtime or event["time_ms"] >= self.runtime["time_ms"]:
                self.runtime = event
        elif event["kind"] == "sample":
            cell = cell_key(event)
            if cell in self.cells and event["time_ms"] < self.cells[cell]["time_ms"]:
                return
            brief = {k: v for k, v in event.items() if k != "curves"}
            self.cells[cell] = brief
            self.cells.move_to_end(cell)
            if event.get("curves"):
                self.traces[cell] = event
            self.history.append(brief)
            while len(self.cells) > 64:
                removed, _ = self.cells.popitem(last=False)
                self.traces.pop(removed, None)

    def poll(self):
        recordings = [r for r in list_recordings(self.directory) if not r["config"].get("demo")]
        if not recordings:
            return
        active_run = recordings[0]["run_id"]
        files = [r for r in recordings if r["run_id"] == active_run]
        if self.run != active_run:
            with self.lock:
                self.run = active_run
                self.config = self.runtime = None
                self.cells.clear(); self.traces.clear(); self.history.clear(); self.seen.clear(); self.positions.clear()
                self.last_received_ms = 0
        for row in sorted(files, key=lambda item: item["modified_ms"]):
            path = self.directory / row["file"]
            try:
                with path.open("rb") as fp:
                    header = fp.readline(MAX_LINE)
                    state = self.positions.get(path.name)
                    if not state or state[0] != header or row["size"] < state[1]:
                        pos = len(header)
                        if row["size"] - pos > READ_BUDGET:
                            fp.seek(row["size"] - READ_BUDGET); fp.readline(MAX_LINE)
                            self.skipped_bytes += fp.tell() - pos; pos = fp.tell()
                        state = (header, pos)
                    fp.seek(state[1])
                    batch = []
                    while fp.tell() - state[1] < READ_BUDGET:
                        before = fp.tell(); line = fp.readline(MAX_LINE + 1)
                        if not line:
                            break
                        if len(line) > MAX_LINE:
                            self.invalid_lines += 1
                            while line and not line.endswith(b"\n") and fp.tell() - state[1] < READ_BUDGET:
                                line = fp.readline(MAX_LINE)
                            continue
                        if not line.endswith(b"\n"):
                            fp.seek(before); break
                        event = decode_line(line) if len(line) <= MAX_LINE else None
                        if event:
                            batch.append(event)
                        else:
                            self.invalid_lines += 1
                    self.positions[path.name] = (header, fp.tell())
                with self.lock:
                    config = decode_line(header)
                    if config:
                        self.ingest(config)
                    for event in batch:
                        self.ingest(event)
            except OSError as exc:
                self.error = str(exc)

    def start(self):
        def loop():
            while not self.stop.is_set():
                try:
                    self.poll()
                except (OSError, ValueError, TypeError) as exc:
                    self.error = str(exc)
                self.stop.wait(0.1)
        self.thread = threading.Thread(target=loop, daemon=True, name="monitor-reader")
        self.thread.start()

    def snapshot(self, selected: str = "") -> dict:
        with self.lock:
            now = time.time() * 1000
            selected = selected if selected in self.cells else next(iter(self.cells), "")
            return dict(schema=1, run_id=self.run, now_ms=now, config=self.config, runtime=self.runtime,
                        connected=bool(self.last_received_ms and now - self.last_received_ms < self.settings["stale_after_ms"]),
                        selected=selected, cells=[dict(event, cell_key=key) for key, event in self.cells.items()],
                        trace=self.traces.get(selected), series=[e for e in self.history if cell_key(e) == selected][-300:],
                        invalid_lines=self.invalid_lines, reader_skipped_bytes=self.skipped_bytes, reader_error=self.error,
                        position={"valid": False, "xyz_m": None, "reason": "not_implemented",
                                  "supported_time_models": ["shared", "receiver_bias"], "used_anchor_ids": []},
                        capabilities={"monitor_only": True, "control": False, "iq_recompute": False, "position_solver": False})


def make_server(settings: dict, store: MonitorStore, port: int | None = None):
    static = Path(__file__).parent / "web"
    gnss=GNSSStore(settings.get("gnss_directory",ROOT/"log/gnss-ui"))

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def send_json(self, value, status=200):
            data = json.dumps(value, ensure_ascii=False, allow_nan=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            parsed = urlsplit(self.path); args = parse_qs(parsed.query)
            try:
                if parsed.path == "/api/bootstrap":
                    self.send_json(dict(settings={k: v for k, v in settings.items() if k in DEFAULTS},
                                        disk_config=settings["disk_config"], record_dir=str(settings["directory"]),
                                        gnss={"offline":True,"live":False,"record_dir":str(gnss.directory)}))
                elif parsed.path == "/api/gnss/runs":
                    self.send_json({"runs":gnss.recordings()})
                elif parsed.path == "/api/gnss/state":
                    self.send_json(gnss.snapshot(args.get("run",[""])[0],
                        time_s=float(args["time"][0]) if "time" in args else None,
                        prn=int(args["prn"][0]) if args.get("prn",[""])[0] else None,
                        window=float(args.get("window",["30"])[0])))
                elif parsed.path == "/api/gnss/frames":
                    self.send_json(gnss.frame_page(args.get("run",[""])[0],float(args.get("time",["0"])[0]),
                        int(args.get("prn",["0"])[0]),int(args.get("offset",["0"])[0]),int(args.get("limit",["20"])[0])))
                elif parsed.path == "/api/state":
                    self.send_json(store.snapshot(args.get("cell", [""])[0]))
                elif parsed.path == "/api/recordings":
                    self.send_json({"recordings": list_recordings(settings["directory"])})
                elif parsed.path == "/api/replay":
                    self.send_json(replay_page(settings["directory"], args.get("file", [""])[0],
                                               int(args.get("offset", ["0"])[0]),
                                               generation=float(args["generation"][0]) if "generation" in args else None))
                elif parsed.path in ("/", "/index.html", "/app.js", "/gnss.js", "/style.css", "/gnss.css"):
                    path = static / ("index.html" if parsed.path == "/" else parsed.path[1:])
                    data = path.read_bytes()
                    if path.suffix == ".html":
                        signal = settings.get("default_signal", "5g")
                        data = data.replace(b'data-default-signal="5g"',
                                            b'data-default-signal="bds"' if signal == "bds" else b'data-default-signal="5g"')
                    mime = {".html": "text/html", ".js": "text/javascript", ".css": "text/css"}[path.suffix]
                    self.send_response(200); self.send_header("Content-Type", mime + "; charset=utf-8")
                    self.send_header("Content-Length", str(len(data))); self.send_header("Cache-Control", "no-cache")
                    self.end_headers(); self.wfile.write(data)
                else:
                    self.send_json({"error": "Not found"}, 404)
            except (ValueError, KeyError, OSError,sqlite3.Error) as exc:
                self.send_json({"error": str(exc)}, 400)
            except (BrokenPipeError, ConnectionResetError):
                pass

    return ThreadingHTTPServer(("127.0.0.1", settings["port"] if port is None else port), Handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "config/config.f", help="Master config path")
    parser.add_argument("--port", type=int, help="Override UI port for this process (1024..65535)")
    parser.add_argument("--signal", choices=("5g", "bds"), default="5g", help="Initial page only; does not start a receiver")
    args = parser.parse_args()
    try:
        settings = load_settings(args.config)
        if args.port is not None:
            if not 1024 <= args.port <= 65535:
                raise ValueError("UI port must be 1024..65535")
            settings["port"] = args.port
        settings["default_signal"] = args.signal
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(2, f"Configuration error: {exc}\n")
    store = MonitorStore(settings); store.start()
    try:
        server = make_server(settings, store)
    except OSError as exc:
        store.stop.set()
        hint="Choose another --port value" if args.port is not None else f"Change port in {settings['ui_path']}"
        parser.exit(2, f"Cannot bind 127.0.0.1:{settings['port']}: {exc}\n{hint} if needed.\n")
    print(f"5G / B1C UI: http://127.0.0.1:{settings['port']}  [read only]", flush=True)
    print(f"Records: {settings['directory']}", flush=True)
    print(f"B1C offline experiments: {settings['gnss_directory']}",flush=True)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        store.stop.set(); server.server_close()
        if store.thread:
            store.thread.join(2)


if __name__ == "__main__":
    main()
