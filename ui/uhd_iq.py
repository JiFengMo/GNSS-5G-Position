"""RX-only UHD C API boundary for raw B210 recording (standard library only).

Uses the public UHD 4.6 C ABI, without the UHD Python module or Boost headers.
No TX streamer, synthetic fallback, filtering or demodulation is provided.
"""
from __future__ import annotations

import ctypes as C
from ctypes.util import find_library
import math
import sys


class TuneRequest(C.Structure):
    _fields_ = [("target", C.c_double), ("rf_policy", C.c_int), ("rf", C.c_double),
                ("dsp_policy", C.c_int), ("dsp", C.c_double), ("args", C.c_char_p)]


class TuneResult(C.Structure):
    _fields_ = [(name, C.c_double) for name in ("clipped", "target_rf", "actual_rf", "target_dsp", "actual_dsp")]


class StreamArgs(C.Structure):
    _fields_ = [("cpu", C.c_char_p), ("wire", C.c_char_p), ("args", C.c_char_p),
                ("channels", C.POINTER(C.c_size_t)), ("count", C.c_int)]


class StreamCommand(C.Structure):
    _fields_ = [("mode", C.c_int), ("samples", C.c_size_t), ("now", C.c_bool),
                ("seconds", C.c_int64), ("fraction", C.c_double)]


class UHDReceiver:
    def __init__(self):
        if sys.byteorder != "little":
            raise RuntimeError("原始 IQ 采集目前要求 little-endian 主机")
        library = find_library("uhd")
        if not library:
            raise RuntimeError("未找到 libuhd；请在已安装 UHD 的 Linux 测试机运行")
        self.lib = C.CDLL(library)
        self.usrp, self.rx, self.md = C.c_void_p(), C.c_void_p(), C.c_void_p()
        self.started = False
        p, size, text = C.c_void_p, C.c_size_t, C.c_char_p
        self.functions = {}
        signatures = {
            "get_last_error": [text, size], "get_version_string": [text, size],
            "string_vector_make": [C.POINTER(p)], "string_vector_free": [C.POINTER(p)],
            "string_vector_size": [p, C.POINTER(size)], "string_vector_at": [p, size, text, size],
            "usrp_find": [text, C.POINTER(p)], "usrp_make": [C.POINTER(p), text], "usrp_free": [C.POINTER(p)],
            "usrp_get_mboard_name": [p, size, text, size],
            "usrp_set_clock_source": [p, text, size], "usrp_set_time_source": [p, text, size],
            "usrp_get_clock_source": [p, size, text, size], "usrp_get_time_source": [p, size, text, size],
            "usrp_set_master_clock_rate": [p, C.c_double, size],
            "usrp_get_master_clock_rate": [p, size, C.POINTER(C.c_double)],
            "usrp_set_rx_rate": [p, C.c_double, size], "usrp_get_rx_rate": [p, size, C.POINTER(C.c_double)],
            "usrp_set_rx_freq": [p, C.POINTER(TuneRequest), size, C.POINTER(TuneResult)],
            "usrp_get_rx_freq": [p, size, C.POINTER(C.c_double)],
            "usrp_set_rx_gain": [p, C.c_double, size, text],
            "usrp_get_rx_gain": [p, size, text, C.POINTER(C.c_double)],
            "usrp_set_rx_bandwidth": [p, C.c_double, size],
            "usrp_get_rx_bandwidth": [p, size, C.POINTER(C.c_double)],
            "usrp_set_rx_antenna": [p, text, size], "usrp_get_rx_antenna": [p, size, text, size],
            "usrp_get_time_now": [p, size, C.POINTER(C.c_int64), C.POINTER(C.c_double)],
            "usrp_get_time_last_pps": [p, size, C.POINTER(C.c_int64), C.POINTER(C.c_double)],
            "usrp_get_mboard_sensor_names": [p, size, C.POINTER(p)],
            "usrp_get_rx_sensor_names": [p, size, C.POINTER(p)],
            "usrp_get_mboard_sensor": [p, text, size, C.POINTER(p)],
            "usrp_get_rx_sensor": [p, text, size, C.POINTER(p)],
            "sensor_value_make": [C.POINTER(p)], "sensor_value_free": [C.POINTER(p)],
            "sensor_value_to_bool": [p, C.POINTER(C.c_bool)],
            "rx_streamer_make": [C.POINTER(p)], "rx_streamer_free": [C.POINTER(p)],
            "rx_metadata_make": [C.POINTER(p)], "rx_metadata_free": [C.POINTER(p)],
            "usrp_get_rx_stream": [p, C.POINTER(StreamArgs), p],
            "rx_streamer_issue_stream_cmd": [p, C.POINTER(StreamCommand)],
            "rx_streamer_recv": [p, C.POINTER(p), size, C.POINTER(p), C.c_double, C.c_bool, C.POINTER(size)],
            "rx_metadata_error_code": [p, C.POINTER(C.c_int)],
            "rx_metadata_has_time_spec": [p, C.POINTER(C.c_bool)],
            "rx_metadata_time_spec": [p, C.POINTER(C.c_int64), C.POINTER(C.c_double)],
            "rx_metadata_out_of_sequence": [p, C.POINTER(C.c_bool)],
        }
        for name, args in signatures.items():
            fn = getattr(self.lib, "uhd_" + name)
            fn.argtypes, fn.restype = args, C.c_int
            self.functions[name] = fn

    def call(self, name, *args):
        code = self.functions[name](*args)
        if code:
            error = C.create_string_buffer(2048)
            self.functions["get_last_error"](error, len(error))
            raise RuntimeError(f"UHD {name} ({code}): {error.value.decode(errors='replace')}")

    def text(self, name, *args):
        value = C.create_string_buffer(4096)
        self.call(name, *args, value, len(value))
        return value.value.decode(errors="replace")

    def number(self, name, *args):
        value = C.c_double()
        self.call(name, *args, C.byref(value))
        return value.value

    def vector(self, name, *args):
        handle = C.c_void_p()
        self.call("string_vector_make", C.byref(handle))
        try:
            self.call(name, *args, C.byref(handle))
            count = C.c_size_t()
            self.call("string_vector_size", handle, C.byref(count))
            return [self.text("string_vector_at", handle, i) for i in range(count.value)]
        finally:
            self.call("string_vector_free", C.byref(handle))

    def sensor(self, name, board=True):
        prefix = "usrp_get_mboard_sensor" if board else "usrp_get_rx_sensor"
        if name not in self.vector(prefix + "_names", self.usrp, 0):
            return None
        value, handle = C.c_bool(), C.c_void_p()
        self.call("sensor_value_make", C.byref(handle))
        try:
            self.call(prefix, self.usrp, name.encode(), 0, C.byref(handle))
            self.call("sensor_value_to_bool", handle, C.byref(value))
            return value.value
        finally:
            self.call("sensor_value_free", C.byref(handle))

    def hardware_time(self, pps=False):
        full, frac = C.c_int64(), C.c_double()
        self.call("usrp_get_time_last_pps" if pps else "usrp_get_time_now",
                  self.usrp, 0, C.byref(full), C.byref(frac))
        return full.value, frac.value

    def open(self, settings, stop):
        args = settings["device_args"]
        if settings["serial"]:
            args += ",serial=" + settings["serial"]
        devices = self.vector("usrp_find", args.encode())
        if len(devices) != 1:
            raise RuntimeError(f"需要唯一 B210；找到 {len(devices)} 台匹配设备，请检查 serial、USB 和设备占用")
        self.call("usrp_make", C.byref(self.usrp), (args + "," + devices[0]).encode())
        board = self.text("usrp_get_mboard_name", self.usrp, 0)
        if "B210" not in board:
            raise RuntimeError(f"所选设备不是 B210：{board}")
        self.call("usrp_set_clock_source", self.usrp, settings["clock_source"].encode(), 0)
        self.call("usrp_set_time_source", self.usrp, settings["time_source"].encode(), 0)
        if settings["master_clock_rate_hz"]:
            self.call("usrp_set_master_clock_rate", self.usrp, settings["master_clock_rate_hz"], 0)
        self.call("usrp_set_rx_rate", self.usrp, settings["sample_rate_hz"], 0)
        tune = TuneRequest(settings["center_freq_hz"], 65, 0, 65, 0, b"")
        self.call("usrp_set_rx_freq", self.usrp, C.byref(tune), 0, C.byref(TuneResult()))
        self.call("usrp_set_rx_gain", self.usrp, settings["rx_gain_db"], 0, b"")
        self.call("usrp_set_rx_bandwidth", self.usrp, settings["bandwidth_hz"], 0)
        self.call("usrp_set_rx_antenna", self.usrp, settings["rx_antenna"].encode(), 0)
        # Bounded wait; unknown lock state must not be treated as confirmed.
        for _ in range(30):
            if stop.is_set():
                raise InterruptedError("采集启动已中断")
            reference = self.sensor("ref_locked")
            lo = self.sensor("lo_locked", board=False)
            if (settings["clock_source"] != "external" or reference is True) and lo is not False:
                break
            stop.wait(.1)
        else:
            raise RuntimeError("参考或接收 LO 未锁定；请检查 clock_source 和外部 10 MHz")
        pps = None
        if settings["time_source"] == "external":
            before = self.hardware_time(pps=True)
            for _ in range(25):
                if stop.wait(.1):
                    raise InterruptedError("PPS 等待已中断")
                if self.hardware_time(pps=True) != before:
                    pps = True
                    break
            if pps is not True:
                raise RuntimeError("未检测到外部 PPS 跳变，请检查 time_source 与 PPS 接线")
        actual = {"device_address": devices[0], "board": board,
                  "uhd_version": self.text("get_version_string"),
                  "sample_rate_hz": self.number("usrp_get_rx_rate", self.usrp, 0),
                  "center_freq_hz": self.number("usrp_get_rx_freq", self.usrp, 0),
                  "rx_gain_db": self.number("usrp_get_rx_gain", self.usrp, 0, b""),
                  "bandwidth_hz": self.number("usrp_get_rx_bandwidth", self.usrp, 0),
                  "master_clock_rate_hz": self.number("usrp_get_master_clock_rate", self.usrp, 0),
                  "rx_antenna": self.text("usrp_get_rx_antenna", self.usrp, 0),
                  "clock_source": self.text("usrp_get_clock_source", self.usrp, 0),
                  "time_source": self.text("usrp_get_time_source", self.usrp, 0),
                  "ref_locked": reference, "lo_locked": lo, "pps_detected": pps}
        if not all(math.isfinite(actual[k]) for k in ("sample_rate_hz", "center_freq_hz", "rx_gain_db", "bandwidth_hz", "master_clock_rate_hz")):
            raise RuntimeError("UHD 回读了非有限射频参数")
        if actual["sample_rate_hz"] <= 0:
            raise RuntimeError("UHD 回读的采样率无效")
        self.call("rx_streamer_make", C.byref(self.rx))
        self.call("rx_metadata_make", C.byref(self.md))
        channel = C.c_size_t(0)
        stream = StreamArgs(b"sc16", b"sc16", b"", C.pointer(channel), 1)
        self.call("usrp_get_rx_stream", self.usrp, C.byref(stream), self.rx)
        self.buffer = C.create_string_buffer(settings["block_samples"] * 4)
        return actual

    def start(self):
        full, frac = self.hardware_time()
        frac += .2
        full += int(frac)
        command = StreamCommand(97, 0, False, full, frac % 1)
        self.call("rx_streamer_issue_stream_cmd", self.rx, C.byref(command))
        self.started = True

    def receive(self, count):
        address = C.c_void_p(C.addressof(self.buffer))
        received, code, timed, sequence = C.c_size_t(), C.c_int(), C.c_bool(), C.c_bool()
        self.call("rx_streamer_recv", self.rx, C.byref(address), count, C.byref(self.md), 1.0, False, C.byref(received))
        self.call("rx_metadata_error_code", self.md, C.byref(code))
        self.call("rx_metadata_out_of_sequence", self.md, C.byref(sequence))
        if code.value or sequence.value:
            raise RuntimeError(f"RX_METADATA code={code.value} out_of_sequence={sequence.value}；停止保存，未将间断拼接为连续 IQ")
        self.call("rx_metadata_has_time_spec", self.md, C.byref(timed))
        if not timed.value or not 0 < received.value <= count:
            raise RuntimeError("接收块缺少硬件时间戳或样本数无效")
        full, frac = C.c_int64(), C.c_double()
        self.call("rx_metadata_time_spec", self.md, C.byref(full), C.byref(frac))
        return C.string_at(address, received.value * 4), full.value, frac.value

    def close(self):
        errors = []
        if self.started:
            try:
                self.call("rx_streamer_issue_stream_cmd", self.rx, C.byref(StreamCommand(111, 0, True, 0, 0)))
            except Exception as exc:
                errors.append(str(exc))
            self.started = False
        for field, function in (("md", "rx_metadata_free"), ("rx", "rx_streamer_free"), ("usrp", "usrp_free")):
            handle = getattr(self, field)
            if handle.value:
                try:
                    self.call(function, C.byref(handle))
                except Exception as exc:
                    errors.append(str(exc))
        if errors:
            raise RuntimeError("UHD 清理失败：" + "; ".join(errors))
