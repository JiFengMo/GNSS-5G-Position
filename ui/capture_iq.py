#!/usr/bin/env python3
"""Raw B210 capture: sh run_5G_only.sh --capture-iq [--config FILE] [--check].

Writes log/iq-capture/<run>/capture.c16, capture.json, capture.meta.txt,
blocks.csv, config.f and capture.log. Next: check complete/continuous_prefix
in capture.json before offline analysis. No demodulation or UI is started.
"""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
from decimal import Decimal, ROUND_HALF_UP, localcontext
import json
import math
import os
from pathlib import Path
import queue
import re
import shutil
import signal
import sys
import threading
import time
import uuid

if __package__:
    from .uhd_iq import UHDReceiver
else:
    from uhd_iq import UHDReceiver

ROOT = Path(__file__).resolve().parents[1]
KEYS = {"device_args", "serial", "clock_source", "time_source", "rx_antenna", "center_freq_hz",
        "sample_rate_hz", "master_clock_rate_hz", "bandwidth_hz", "rx_gain_db", "duration_s",
        "block_samples", "queue_blocks", "record_dir", "ssb_scs_khz", "ssb_period_ms",
        "ssb_freq_offset_hz", "target_pci", "nr_band"}
NUMBERS = KEYS - {"device_args", "serial", "clock_source", "time_source", "rx_antenna", "record_dir"}


def settings_from_file(path, root=ROOT):
    path = Path(path).resolve()
    text = path.read_text(encoding="utf-8-sig")
    job = {}
    for no, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{no}: 需要 key = value")
        key, value = (part.strip() for part in line.split("=", 1))
        if key in job or (not value and key != "serial"):
            raise ValueError(f"{path}:{no}: 重复配置项或值为空")
        job[key] = value
    if set(job) != KEYS:
        raise ValueError(f"采集配置存在未知/缺失项：{sorted(set(job) ^ KEYS)}")
    for key in NUMBERS:
        job[key] = float(job[key])
        if not math.isfinite(job[key]):
            raise ValueError(f"{key} 必须为有限数值")
    for key in ("block_samples", "queue_blocks", "ssb_scs_khz", "ssb_period_ms", "target_pci", "nr_band"):
        if job[key] != int(job[key]):
            raise ValueError(f"{key} 必须为整数")
        job[key] = int(job[key])
    if not 1e6 <= job["sample_rate_hz"] <= 61.44e6 or not 0 < job["duration_s"] <= 60:
        raise ValueError("采样率需在 1～61.44 Msps，采集时长需在 (0,60] 秒")
    if not 70e6 <= job["center_freq_hz"] <= 6e9 or not 0 <= job["rx_gain_db"] <= 76:
        raise ValueError("B210 中心频率或接收增益超出允许范围")
    if not 0 < job["bandwidth_hz"] <= 56e6 or not 0 <= job["master_clock_rate_hz"] <= 61.44e6:
        raise ValueError("接收带宽/主时钟无效；主时钟 0 表示由 UHD 选择")
    if not 4096 <= job["block_samples"] <= 1048576 or not 2 <= job["queue_blocks"] <= 128:
        raise ValueError("block_samples 需在 4096～1048576，queue_blocks 需在 2～128")
    if job["block_samples"] * job["queue_blocks"] * 4 > 512 * 1024**2:
        raise ValueError("采集队列不得超过 512 MiB")
    if job["ssb_scs_khz"] not in (15, 30) or job["ssb_period_ms"] not in (5, 10, 20, 40, 80, 160):
        raise ValueError("SSB SCS/周期提示值无效")
    if not -1 <= job["target_pci"] <= 1007 or job["nr_band"] < -1:
        raise ValueError("target_pci/nr_band 提示值无效")
    if any(job[k] not in ("internal", "external") for k in ("clock_source", "time_source")):
        raise ValueError("clock_source/time_source 可选 internal 或 external")
    if job["rx_antenna"] not in ("RX2", "TX/RX") or not re.fullmatch(r"[A-Za-z0-9_-]*", job["serial"]):
        raise ValueError("rx_antenna 或 serial 无效")
    if any(p.strip().split("=", 1)[0] == "serial" for p in job["device_args"].split(",")):
        raise ValueError("序列号请填写独立的 serial 项，不要放入 device_args")
    relative = Path(job["record_dir"])
    log_root = (Path(root) / "log").resolve()
    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
        raise ValueError("record_dir 必须为 log/ 下的相对目录，不能含 ..")
    directory = (log_root / relative).resolve()
    if log_root not in directory.parents:
        raise ValueError("采集目录必须位于工程 log/ 内")
    return job, directory, text


def ensure_space(directory, samples):
    existing = Path(directory)
    while not existing.exists():
        existing = existing.parent
    # Include room for metadata, packet index and final status on disk-full.
    required = samples * 4 + 64 * 1024**2
    if shutil.disk_usage(existing).free < required:
        raise OSError(f"磁盘空间不足；至少需要约 {required / 1024**2:.1f} MiB")


def sample_tick(full, fraction, rate):
    if not isinstance(full, int) or full < 0 or not math.isfinite(fraction) or not 0 <= fraction < 1:
        raise RuntimeError("无效的硬件时间戳")
    with localcontext() as context:
        context.prec = 50
        tick = (Decimal(full) + Decimal.from_float(fraction)) * Decimal.from_float(rate)
        value = int(tick.to_integral_value(rounding=ROUND_HALF_UP))
    if not 0 <= value <= 2**64 - 1:
        raise RuntimeError("硬件采样计数超出 uint64 范围")
    return value


def atomic_json(path, value):
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temp.replace(path)


class DiskWriter:
    """One bounded queue; disk stalls never block RX without detection."""
    def __init__(self, directory, blocks):
        self.queue = queue.Queue(blocks)
        self.done = threading.Event()
        self.error = None
        self.samples = 0
        self.blocks = 0
        self.directory = directory
        self.thread = threading.Thread(target=self.work, name="iq-writer")
        self.thread.start()

    def write(self, data, full, fraction, tick):
        if self.error:
            raise OSError("写入线程失败：" + str(self.error))
        try:
            self.queue.put_nowait((data, full, fraction, tick))
        except queue.Full:
            raise RuntimeError("磁盘写入队列已满；停止采集，未静默丢弃 IQ") from None

    def work(self):
        try:
            with (self.directory / "capture.c16").open("xb") as raw, (self.directory / "blocks.csv").open("x", newline="", encoding="utf-8") as index:
                rows = csv.writer(index)
                rows.writerow(("block", "file_offset_bytes", "samples", "hardware_sample", "time_full_s", "time_frac_s"))
                while not self.done.is_set() or not self.queue.empty():
                    try:
                        data, full, fraction, tick = self.queue.get(timeout=.1)
                    except queue.Empty:
                        continue
                    if raw.write(data) != len(data):
                        raise OSError("IQ 文件短写")
                    rows.writerow((self.blocks, self.samples * 4, len(data) // 4, str(tick), str(full), repr(fraction)))
                    self.samples += len(data) // 4
                    self.blocks += 1
                raw.flush(); os.fsync(raw.fileno())
                index.flush(); os.fsync(index.fileno())
        except Exception as exc:
            self.error = exc

    def finish(self):
        self.done.set()
        self.thread.join()
        if self.error:
            raise OSError("IQ/索引写入失败：" + str(self.error))


def capture(settings, directory, config_text, stop, radio_factory=UHDReceiver):
    """radio_factory is injectable by unit tests only; CLI always uses real UHD."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    (directory / "config.f").write_text(config_text, encoding="utf-8")
    result = {"schema": 1, "kind": "raw_iq_capture", "status": "initializing", "complete": False,
              "continuous_prefix": False, "format": "sc16_le", "layout": "I0,Q0,I1,Q1,...",
              "bytes_per_complex_sample": 4, "channels": 1, "cpu_format": "sc16", "wire_format": "sc16",
              "started_utc": datetime.now(timezone.utc).isoformat(), "requested": settings,
              "actual": None, "target_samples": 0, "samples_received": 0, "samples_written": 0,
              "blocks_written": 0, "first_hardware_sample": None, "first_time_full_s": None,
              "first_time_frac_s": None, "absolute_utc_known": False, "error": None}
    manifest = directory / "capture.json"
    atomic_json(manifest, result)
    radio = writer = None
    expected = None
    code = 1
    with (directory / "capture.log").open("x", encoding="utf-8") as log:
        def report(message):
            print(message, flush=True)
            log.write(message + "\n"); log.flush()
        try:
            report("原始 IQ 采集目录：" + str(directory))
            radio = radio_factory()
            actual = radio.open(settings, stop)
            result["actual"] = actual
            fs = actual["sample_rate_hz"]
            if not math.isfinite(fs) or not 1e6 <= fs <= 61.44e6 + 1:
                raise RuntimeError("实际采样率超出当前原始采集范围")
            target = int(math.floor(fs * settings["duration_s"] + .5))
            if target < 1:
                raise RuntimeError("采集时长不足一个复数样本")
            result["target_samples"] = target
            ensure_space(directory, target)
            report(f"实际采样率 {fs:.17g} S/s，中心频率 {actual['center_freq_hz']:.17g} Hz；目标 {target} 个复数样本（约 {target*4/1e6:.1f} MB）")
            writer = DiskWriter(directory, settings["queue_blocks"])
            result["status"] = "capturing"
            atomic_json(manifest, result)
            if stop.is_set():
                raise InterruptedError("采集启动已中断")
            radio.start()
            next_report = time.monotonic() + 1
            while result["samples_received"] < target:
                if stop.is_set():
                    raise InterruptedError("用户中断；已保存的连续前缀保留")
                want = min(settings["block_samples"], target - result["samples_received"])
                data, full, fraction = radio.receive(want)
                count = len(data) // 4
                if len(data) % 4 or not 0 < count <= want:
                    raise RuntimeError("原始 IQ 块长度错误")
                tick = sample_tick(full, fraction, fs)
                if expected is not None and tick != expected:
                    raise RuntimeError(f"采样时间间断/倒退：expected={expected}, actual={tick}；已保存前缀保留")
                writer.write(data, full, fraction, tick)
                if expected is None:
                    result.update(first_hardware_sample=str(tick), first_time_full_s=str(full), first_time_frac_s=fraction)
                expected = tick + count
                result["samples_received"] += count
                if time.monotonic() >= next_report:
                    result.update(samples_written=writer.samples, blocks_written=writer.blocks)
                    atomic_json(manifest, result)
                    report(f"已接收 {result['samples_received'] / fs:.2f}/{target / fs:.2f} 秒；写入 {writer.samples*4/1e6:.1f} MB；队列 {writer.queue.qsize()} 块")
                    next_report = time.monotonic() + 1
            code = 0
        except InterruptedError as exc:
            result["error"] = str(exc); code = 130
        except Exception as exc:
            result["error"] = str(exc)
        finally:
            # Stop RX before draining/flushing disk; do not leave the device streaming.
            if radio is not None:
                try:
                    radio.close()
                except Exception as exc:
                    result["error"] = (result["error"] or "") + "; " + str(exc); code = 1
            if writer is not None:
                try:
                    writer.finish()
                except Exception as exc:
                    result["error"] = (result["error"] or "") + "; " + str(exc); code = 1
                result.update(samples_written=writer.samples, blocks_written=writer.blocks)
            result["complete"] = code == 0 and result["samples_written"] == result["target_samples"]
            if code == 0 and not result["complete"]:
                result["error"] = "实际写入样本数未达到目标"
                code = 1
            result["status"] = "complete" if result["complete"] else "interrupted" if code == 130 else "failed"
            result["finished_utc"] = datetime.now(timezone.utc).isoformat()
            actual = result["actual"]
            if actual:
                result["saved_duration_s"] = result["samples_written"] / actual["sample_rate_hz"]
                metadata = {"tag": "raw_iq_capture", "format": "sc16_le", "fs_hz": repr(actual["sample_rate_hz"]),
                            "center_freq_hz": repr(actual["center_freq_hz"]), "nsamps": result["samples_written"],
                            "ts_first": result["first_hardware_sample"] or "unknown", "complete": int(result["complete"])}
                metadata.update({k: settings[k] for k in ("ssb_scs_khz", "ssb_period_ms", "ssb_freq_offset_hz", "target_pci", "nr_band")})
                (directory / "capture.meta.txt").write_text("".join(f"{k}={v}\n" for k, v in metadata.items()), encoding="utf-8")
            result["file_bytes"] = (directory / "capture.c16").stat().st_size if (directory / "capture.c16").exists() else 0
            result["continuous_prefix"] = (result["samples_written"] > 0 and
                                            result["file_bytes"] == result["samples_written"] * 4 and
                                            writer is not None and writer.error is None)
            atomic_json(manifest, result)
            report(f"采集结果：{result['status']}；已保存 {result['samples_written']} 个复数样本。")
            if result["error"]:
                report("原因：" + result["error"])
    return code, result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "config/5G/iq_capture.f", help="独立原始 IQ 配置")
    parser.add_argument("--check", action="store_true", help="只检查配置与磁盘空间；不加载 UHD，不打开设备")
    args = parser.parse_args(argv)
    try:
        job, parent, config_text = settings_from_file(args.config)
        ensure_space(parent, int(math.ceil(job["sample_rate_hz"] * job["duration_s"])))
        print("原始 IQ 配置：" + str(args.config.resolve()), flush=True)
        print(f"请求值：{job['center_freq_hz'] / 1e6:g} MHz，{job['sample_rate_hz'] / 1e6:g} MS/s，{job['duration_s']:g} 秒，serial={job['serial'] or '自动（要求唯一设备）'}", flush=True)
        if args.check:
            print("配置与空间检查通过；未打开设备。输出根目录：" + str(parent))
            return 0
        stop = threading.Event()
        previous = {}
        def interrupt(signum, frame):
            stop.set()
        for sig in (signal.SIGINT, signal.SIGTERM):
            previous[sig] = signal.signal(sig, interrupt)
        try:
            run = datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
            code, _ = capture(job, parent / run, config_text, stop)
            return code
        finally:
            for sig, handler in previous.items():
                signal.signal(sig, handler)
    except (OSError, ValueError, RuntimeError) as exc:
        print("原始 IQ 采集失败：" + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
