"""Compare the built SDK against the user's actual Python upper-computer protocol.

Usage: python python_wire_diff.py --python-root D:/CODE2/fpga_upper
                                --wire-test ../build/wire_test.exe
No copied protocol implementation is used as the reference.
"""

from __future__ import annotations

import argparse
import importlib
import math
from pathlib import Path
import struct
import subprocess
import sys


def raw(value: str) -> bytes:
    return b"" if value == "-" else bytes.fromhex(value)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--python-root", required=True, type=Path)
    parser.add_argument("--wire-test", required=True, type=Path)
    args = parser.parse_args()
    sys.path.insert(0, str(args.python_root.resolve()))
    control = importlib.import_module("protocol.control_frame")
    transport = importlib.import_module("protocol.transport_frame")
    parameter = importlib.import_module("protocol.parameter_values")
    interpolation = importlib.import_module("protocol.interpolation_config")
    crc = importlib.import_module("protocol.crc16")
    crc.set_crc_verify(True)
    completed = subprocess.run([str(args.wire_test.resolve()), "--vectors"],
                               check=True, capture_output=True, text=True)
    counts: dict[str, int] = {}
    command_codes: set[int] = set()
    for number, line in enumerate(completed.stdout.splitlines(), 1):
        parts = line.split()
        kind = parts[0]
        counts[kind] = counts.get(kind, 0) + 1
        expected = raw(parts[-1]) if kind != "S" else None
        if kind == "C":
            dev, cmd, mid, typ, rw = map(int, parts[1:6])
            command_codes.add(cmd)
            payload = raw(parts[6])
            actual = control.pack_cmd_frame(dev, cmd, mid, typ, rw, payload)
            decoded = control.unpack_cmd_frame(expected)
            assert decoded["device_type"] == dev and decoded["cmd_code"] == cmd
            assert decoded["msg_id"] == mid and decoded["msg_type"] == typ
            assert decoded["rw"] == rw and decoded["data"] == payload
        elif kind == "T":
            dev, pid, total, current, retransmit = map(int, parts[1:6])
            payload = raw(parts[6])
            actual = transport.pack_transport_frame(payload, packet_id=pid,
                total_packets=total, current_packet=current, retransmit=retransmit,
                device_type=dev)
            decoded = transport.unpack_transport_frame(expected)
            assert decoded["device_type"] == dev and decoded["packet_id"] == pid
            assert decoded["total_packets"] == total and decoded["current_packet"] == current
            assert decoded["retransmit"] == retransmit and decoded["data"] == payload
        elif kind == "N":
            actual = control.pack_network_param(*parts[1:5], raw(parts[5]))
            decoded = control.unpack_network_param(expected)
            assert decoded["device_ip"] == parts[1] and decoded["mac"] == raw(parts[5])
        elif kind == "I":
            start, end, interval, count = map(int, parts[1:5])
            cfg = interpolation.InterpolationConfig.from_mm(start / 65536,
                end / 65536, interval / 65536, count)
            actual = cfg.payload
        elif kind == "A":
            values = list(map(float, parts[1:-1]))
            actual = control.pack_calib_data(values)
        elif kind == "CM":
            actual = control.pack_cmos_area(*map(int, parts[1:5]))
        elif kind == "S":
            rate, cpp_ticks, cpp_rate = float(parts[1]), int(parts[2]), float(parts[3])
            actual_ticks = parameter.sample_rate_to_ticks(rate)
            actual_rate = parameter.sample_ticks_to_rate(actual_ticks)
            assert actual_ticks == cpp_ticks, f"sample ticks differ at line {number}"
            assert math.isclose(actual_rate, cpp_rate, rel_tol=1e-15), f"sample rate differs at line {number}"
            continue
        else:
            raise AssertionError(f"unknown differential vector {kind}")
        assert actual == expected, f"Python/C++ bytes differ at line {number}, operation={kind}"

    python_commands = {value for name, value in vars(control).items()
                       if name.startswith("CMD_") and name not in
                       ("CMD_HEADER", "CMD_TAIL", "CMD_HEADER_BYTES", "CMD_TAIL_BYTES")
                       and isinstance(value, int)}
    assert command_codes == python_commands | {0x0012, 0x0013}, f"command coverage differs: missing={python_commands-command_codes}"
    print(f"Python/C++ differential passed: {sum(counts.values())} vectors, "
          f"{len(python_commands)} Python commands + 2 documented HDR commands, sizes 0..8192 bytes; {counts}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
