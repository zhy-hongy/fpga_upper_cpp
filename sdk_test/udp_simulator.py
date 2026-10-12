#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FPGA UDP Simulator - for testing CameraSDK.dll UDP communication

Protocol:
  - SDK sends control commands to FPGA_IP:dataPort (default 60000)
  - SDK listens on controlPort (default 5000) for ACK responses
  - This simulator listens on dataPort, parses commands, and sends ACK back to controlPort

Frame structure (two layers):
  Outer Transport Frame: [0xABAB][2B packet_id][2B total][2B current][4B retransmit][4B reserved][2B data_len][payload][2B CRC16][0xDCDC]
  Inner Control Frame:   [0xAA55][2B device_type][2B cmd_code][2B msg_id][1B msg_type][1B rw][4B data_len][payload][2B CRC16][0xFAFA]

Usage:
  python udp_simulator.py [data_port=60000] [control_port=5000]
"""

import os
import select
import socket
import struct
import sys
import time
from datetime import datetime

# -- Force unbuffered output so logs appear in real time --
sys.stdout.reconfigure(line_buffering=True)  # Python 3.7+
sys.stderr.reconfigure(line_buffering=True)

# -- 管理控制（admin）端口：用于自动化测试时暂停/恢复 ACK ---------------
# 由于 Windows SO_REUSEADDR 下"先绑定者拥有端口"，自动化测试不能通过
# 停止/重启本进程来模拟 FPGA 无应答（重启后的新进程会被 SDK 自己的数据
# socket 遮蔽，收不到命令）。因此提供一个独立的 admin 端口（data_port+1）：
#   向该端口发送 "PAUSE"  -> 收到命令后不再回 ACK（模拟无应答/超时路径）
#   向该端口发送 "RESUME" -> 恢复回 ACK
ADMIN_PORT_OFFSET = 1

ACK_ENABLED = True   # 由 admin 端口控制

# -- Log file (for real-time viewing when stdout is piped) --
LOG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "udp_simulator.log")
_log_fp = None

def log(msg: str):
    """Write to stdout and log file simultaneously, with flush."""
    print(msg, flush=True)
    global _log_fp
    if _log_fp is None:
        try:
            _log_fp = open(LOG_FILE, 'w', encoding='utf-8')
        except Exception:
            pass
    if _log_fp:
        _log_fp.write(msg + '\n')
        _log_fp.flush()

# -- CRC16-MODBUS -----------------------------------------------------------
# poly=0x8005, init=0xFFFF, ref_in=True, ref_out=True, xor_out=0x0000

_CRC_TABLE = None

def _make_crc_table():
    global _CRC_TABLE
    if _CRC_TABLE is not None:
        return _CRC_TABLE
    _CRC_TABLE = []
    for i in range(256):
        crc = i
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
        _CRC_TABLE.append(crc)
    return _CRC_TABLE

def crc16_modbus(data: bytes) -> int:
    table = _make_crc_table()
    crc = 0xFFFF
    for b in data:
        crc = (crc >> 8) ^ table[(crc ^ b) & 0xFF]
    return crc

# -- Constants --------------------------------------------------------------

TRANSPORT_HEADER = 0xABAB
TRANSPORT_TAIL   = 0xDCDC
CMD_HEADER       = 0xAA55
CMD_TAIL         = 0xFAFA

MSG_TYPE_COMMAND  = 0x00
MSG_TYPE_RESPONSE = 0x01
MSG_TYPE_EVENT    = 0x02

RW_WRITE = 0x40
RW_READ  = 0x80

# Command code names
CMD_NAMES = {
    0x0001: "EXPOSURE_TIME",
    0x0002: "GAIN",
    0x0003: "ROI_X",
    0x0007: "CAMERA_VERSION",
    0x000A: "SAMPLE_RATE",
    0x0101: "TRIGGER_MODE",
    0x0102: "ENCODER_TYPE",
    0x0103: "ENCODER_MODE",
    0x0104: "ENCODER_INTERVAL",
    0x0201: "BATCH_LENGTH",
    0x0202: "EXTERNAL_TRIGGER_START",
    0x0203: "BATCH_CONTROL",
    0x0204: "GET_BATCH_DATA_REALTIME",
    0x0205: "GET_ALL_BATCH_DATA",
    0x0206: "RESET_BUFFER",
    0x0501: "NETWORK_CONFIG",
    0x0502: "FPGA_VERSION",
    0x0503: "DEVICE_SERIAL",
}

DEVICE_NAMES = {
    0x0000: "FPGA",
    0x0001: "SENSOR1",
    0x0002: "SENSOR2",
    0x0003: "SENSOR3",
    0x0004: "SENSOR4",
}

# Simulated device state (for read commands to return reasonable values)
MOCK_STATE = {
    "exposure_time": 1000,    # us
    "gain": 200,
    "roi_x": 0,               # 高16位起始lane，低16位lane长度
    "camera_version": b'\x01\x00\x00\x01',   # "1.0.0.1"
    "fpga_version": b'\x01\x00\x00\x01',     # "1.0.0.1"
    "device_serial": 0x12345678,
    "sample_rate": 200,
    "trigger_mode": 1,        # continuous
    "encoder_type": 1,        # differential
    "encoder_mode": 1,        # 4x
    "encoder_interval": 1000,
    "batch_length": 16,
    "external_trigger": 0,    # off
    "net_dev_ip": 0xC0A80A0A,     # 192.168.10.10
    "net_pc_ip": 0xC0A80A64,      # 192.168.10.100
    "net_mask": 0xFFFFFF00,       # 255.255.255.0
    "net_gw": 0xC0A80A01,         # 192.168.10.1
    "net_mac": bytes([0x00, 0x00, 0x02, 0x00, 0x00, 0x01]),
}

# -- Frame parsing & construction -------------------------------------------

def parse_transport_frame(data: bytes) -> dict | None:
    """Parse outer Transport Frame, return inner payload"""
    if len(data) < 24:
        return None
    if data[0:2] != b'\xAB\xAB':
        return None
    if data[-2:] != b'\xDC\xDC':
        return None

    # CRC check
    received_crc = struct.unpack('>H', data[-4:-2])[0]
    computed_crc = crc16_modbus(data[2:-4])
    if received_crc != computed_crc:
        log(f"  [WARN] Transport CRC mismatch: got 0x{received_crc:04X}, expected 0x{computed_crc:04X}")
        return None

    device_type   = struct.unpack('>H', data[2:4])[0]
    packet_id     = struct.unpack('>H', data[4:6])[0]
    total_packets = struct.unpack('>H', data[6:8])[0]
    current_pkt   = struct.unpack('>H', data[8:10])[0]
    retransmit    = struct.unpack('>I', data[10:14])[0]
    data_len      = struct.unpack('>H', data[18:20])[0]

    payload = data[20:20 + data_len]

    return {
        'device_type': device_type,
        'packet_id': packet_id,
        'total_packets': total_packets,
        'current_packet': current_pkt,
        'retransmit': retransmit,
        'payload': payload,
    }


def parse_control_frame(data: bytes) -> dict | None:
    """Parse inner Control Frame"""
    if len(data) < 18:
        return None
    if struct.unpack('>H', data[0:2])[0] != CMD_HEADER:
        return None
    if struct.unpack('>H', data[-2:])[0] != CMD_TAIL:
        return None

    # CRC check
    received_crc = struct.unpack('>H', data[-4:-2])[0]
    computed_crc = crc16_modbus(data[2:-4])
    if received_crc != computed_crc:
        log(f"  [WARN] Control CRC mismatch: got 0x{received_crc:04X}, expected 0x{computed_crc:04X}")
        return None

    device_type = struct.unpack('>H', data[2:4])[0]
    cmd_code    = struct.unpack('>H', data[4:6])[0]
    msg_id      = struct.unpack('>H', data[6:8])[0]
    msg_type    = data[8]
    rw          = data[9]
    data_len    = struct.unpack('>I', data[10:14])[0]
    payload     = data[14:14 + data_len]

    return {
        'device_type': device_type,
        'cmd_code': cmd_code,
        'msg_id': msg_id,
        'msg_type': msg_type,
        'rw': rw,
        'payload': payload,
    }


def pack_control_frame(device_type: int, cmd_code: int, msg_id: int,
                       msg_type: int, rw: int, payload: bytes = b'') -> bytes:
    """Build inner Control Frame"""
    inner = struct.pack('>HHHBBI',
                        device_type, cmd_code, msg_id,
                        msg_type, rw, len(payload))
    inner += payload
    crc = crc16_modbus(inner)
    return struct.pack('>H', CMD_HEADER) + inner + struct.pack('>H', crc) + struct.pack('>H', CMD_TAIL)


def pack_transport_frame(payload: bytes, packet_id: int = 0, device_type: int = 0) -> bytes:
    """Build outer Transport Frame"""
    inner = struct.pack('>HHHHIIH',
                        device_type, packet_id, 1, 1,  # device_type, packet_id, total=1, current=1
                        0, 0,             # retransmit=0, reserved=0
                        len(payload))
    inner += payload
    crc = crc16_modbus(inner)
    return struct.pack('>H', TRANSPORT_HEADER) + inner + struct.pack('>H', crc) + struct.pack('>H', TRANSPORT_TAIL)


def get_response_data(cmd_code: int, rw: int) -> bytes:
    """Return simulated response data based on command code and read/write type"""
    if rw != RW_READ:
        return b''

    # 版本号：4B "A.B.C.D" 字节串（直接返回）
    if cmd_code == 0x0007:      # CAMERA_VERSION
        return bytes(MOCK_STATE["camera_version"])
    if cmd_code == 0x0502:      # FPGA_VERSION
        return bytes(MOCK_STATE["fpga_version"])

    # 网络配置：24B (设备IP + 本机IP + 掩码 + 网关 + 2B 预留 + MAC6)
    if cmd_code == 0x0501:      # NETWORK_CONFIG
        return (struct.pack('>IIII',
                            MOCK_STATE["net_dev_ip"],
                            MOCK_STATE["net_pc_ip"],
                            MOCK_STATE["net_mask"],
                            MOCK_STATE["net_gw"])
                + b'\x00\x00'
                + bytes(MOCK_STATE["net_mac"]))

    # 4-byte big-endian values
    four_byte_cmds = {
        0x0001: MOCK_STATE["exposure_time"],     # EXPOSURE_TIME
        0x0002: MOCK_STATE["gain"],               # GAIN
        0x0003: MOCK_STATE["roi_x"],              # ROI_X
        0x000A: MOCK_STATE["sample_rate"],        # SAMPLE_RATE
        0x0104: MOCK_STATE["encoder_interval"],   # ENCODER_INTERVAL
        0x0503: MOCK_STATE["device_serial"],      # DEVICE_SERIAL
    }

    # 2-byte big-endian values
    two_byte_cmds = {
        0x0101: MOCK_STATE["trigger_mode"],       # TRIGGER_MODE
        0x0102: MOCK_STATE["encoder_type"],       # ENCODER_TYPE
        0x0103: MOCK_STATE["encoder_mode"],       # ENCODER_MODE
        0x0201: MOCK_STATE["batch_length"],       # BATCH_LENGTH
        0x0202: MOCK_STATE["external_trigger"],   # EXTERNAL_TRIGGER_START
    }

    if cmd_code in four_byte_cmds:
        return struct.pack('>I', four_byte_cmds[cmd_code])
    elif cmd_code in two_byte_cmds:
        return struct.pack('>H', two_byte_cmds[cmd_code])
    else:
        # Default: 2 bytes of zero
        return b'\x00\x00'


def handle_command(cmd: dict, sender_addr: tuple, sock: socket.socket, control_port: int):
    """Handle received command, build ACK response and send"""
    device_type = cmd['device_type']
    cmd_code    = cmd['cmd_code']
    msg_id      = cmd['msg_id']
    msg_type    = cmd['msg_type']
    rw          = cmd['rw']
    payload     = cmd['payload']
    sender_ip   = sender_addr[0]

    dev_name = DEVICE_NAMES.get(device_type, f"0x{device_type:04X}")
    cmd_name = CMD_NAMES.get(cmd_code, f"0x{cmd_code:04X}")
    rw_name  = "WRITE" if rw == RW_WRITE else ("READ" if rw == RW_READ else f"0x{rw:02X}")

    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    log(f"[{ts}] <- CMD #{msg_id:5d}  {rw_name:5s} {cmd_name:20s}  Dev={dev_name:7s}  Payload={len(payload):3d}B")
    if payload:
        log(f"                   Payload hex: {payload.hex()}")

    # Write commands: update simulated state
    if rw == RW_WRITE:
        if cmd_code == 0x0001 and len(payload) >= 4:  # EXPOSURE_TIME
            MOCK_STATE["exposure_time"] = struct.unpack('>I', payload[:4])[0]
        elif cmd_code == 0x0002 and len(payload) >= 4:  # GAIN
            MOCK_STATE["gain"] = struct.unpack('>I', payload[:4])[0]
        elif cmd_code == 0x0003 and len(payload) >= 4:  # ROI_X
            MOCK_STATE["roi_x"] = struct.unpack('>I', payload[:4])[0]
        elif cmd_code == 0x000A and len(payload) >= 4:  # SAMPLE_RATE
            MOCK_STATE["sample_rate"] = struct.unpack('>I', payload[:4])[0]
        elif cmd_code == 0x0101 and len(payload) >= 2:  # TRIGGER_MODE
            MOCK_STATE["trigger_mode"] = struct.unpack('>H', payload[:2])[0]
        elif cmd_code == 0x0102 and len(payload) >= 2:  # ENCODER_TYPE
            MOCK_STATE["encoder_type"] = struct.unpack('>H', payload[:2])[0]
        elif cmd_code == 0x0103 and len(payload) >= 2:  # ENCODER_MODE
            MOCK_STATE["encoder_mode"] = struct.unpack('>H', payload[:2])[0]
        elif cmd_code == 0x0104 and len(payload) >= 4:  # ENCODER_INTERVAL
            MOCK_STATE["encoder_interval"] = struct.unpack('>I', payload[:4])[0]
        elif cmd_code == 0x0201 and len(payload) >= 2:  # BATCH_LENGTH
            MOCK_STATE["batch_length"] = struct.unpack('>H', payload[:2])[0]
        elif cmd_code == 0x0202 and len(payload) >= 2:  # EXTERNAL_TRIGGER_START
            MOCK_STATE["external_trigger"] = struct.unpack('>H', payload[:2])[0]
        elif cmd_code == 0x0501 and len(payload) >= 24:  # NETWORK_CONFIG
            MOCK_STATE["net_dev_ip"], MOCK_STATE["net_pc_ip"], \
            MOCK_STATE["net_mask"], MOCK_STATE["net_gw"] = struct.unpack('>IIII', payload[:16])
            MOCK_STATE["net_mac"] = payload[18:24]  # 跳过 2B 预留
        elif cmd_code == 0x0503 and len(payload) >= 4:  # DEVICE_SERIAL
            MOCK_STATE["device_serial"] = struct.unpack('>I', payload[:4])[0]

    # Build response
    response_data = get_response_data(cmd_code, rw)
    response_cf = pack_control_frame(
        device_type, cmd_code, msg_id,
        MSG_TYPE_RESPONSE, RW_READ, response_data
    )
    response_tf = pack_transport_frame(response_cf, device_type=device_type)

    if not ACK_ENABLED:
        # 模拟 FPGA 无应答：记录到日志但不发送 ACK（用于超时测试路径）。
        ts2 = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        log(f"[{ts2}] ~ ACK DISABLED, NOT replying to {sender_ip}:{control_port}")
        log("")
        return

    # Send response to SDK's controlPort
    sock.sendto(response_tf, (sender_ip, control_port))

    ts2 = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    ack_info = f"RespData={len(response_data):3d}B" if response_data else "ACK"
    log(f"[{ts2}] -> {ack_info:20s}  -> {sender_ip}:{control_port}")
    log("")  # blank line between commands


def format_hex_dump(data: bytes, max_len: int = 80) -> str:
    """Format bytes as hex string, truncated if too long"""
    if len(data) <= max_len:
        return data.hex()
    return data[:max_len].hex() + f"... ({len(data)}B total)"


def main():
    data_port    = int(sys.argv[1]) if len(sys.argv) > 1 else 60000
    control_port = int(sys.argv[2]) if len(sys.argv) > 2 else 5000
    admin_port   = data_port + ADMIN_PORT_OFFSET

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', data_port))
    sock.setblocking(False)

    admin_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    admin_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    admin_sock.bind(('0.0.0.0', admin_port))
    admin_sock.setblocking(False)

    log("=" * 70)
    log(f"  FPGA UDP Simulator")
    log(f"  Listening on (dataPort) : 0.0.0.0:{data_port}")
    log(f"  ACK reply to (ctrlPort): sender_ip:{control_port}")
    log(f"  Admin control (adminPort): 0.0.0.0:{admin_port}  (PAUSE / RESUME)")
    log(f"  Simulated device state : {MOCK_STATE}")
    log("=" * 70)
    log("  Waiting for SDK commands... (Ctrl+C to stop)")
    log("")

    cmd_count = 0
    raw_count = 0
    try:
        while True:
            # 同时监听数据端口与 admin 端口（非阻塞 + select，避免忙轮询）
            readable, _, _ = select.select([sock, admin_sock], [], [], 0.5)
            for rsock in readable:
                if rsock is admin_sock:
                    try:
                        admin_data, _addr = admin_sock.recvfrom(64)
                    except (BlockingIOError, OSError):
                        continue
                    cmd = admin_data.decode('utf-8', errors='replace').strip().upper()
                    global ACK_ENABLED
                    if cmd == "PAUSE":
                        ACK_ENABLED = False
                        log(f"[admin] >>> ACK DISABLED (simulating FPGA no-response)")
                    elif cmd == "RESUME":
                        ACK_ENABLED = True
                        log(f"[admin] >>> ACK ENABLED")
                    else:
                        log(f"[admin] unknown cmd: {cmd!r}")
                    continue

                # ---- 数据端口 ----
                try:
                    data, addr = sock.recvfrom(65536)
                except (BlockingIOError, OSError):
                    continue

                raw_count += 1
                ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]

                # Always print raw packet info
                log(f"[{ts}] RAW UDP #{raw_count:3d}  {len(data):4d}B from {addr[0]}:{addr[1]}  "
                      f"hex: {format_hex_dump(data)}")

                # Parse Transport Frame
                tf = parse_transport_frame(data)
                if tf is None:
                    log(f"       [WARN] Not a valid Transport Frame (header=0xABAB, tail=0xDCDC)")
                    log("")
                    continue

                payload = tf['payload']

                # Parse Control Frame
                cf = parse_control_frame(payload)
                if cf is None:
                    log(f"       [WARN] Not a valid Control Frame (header=0xAA55, tail=0xFAFA)")
                    log(f"       Inner payload hex: {payload.hex()}")
                    log("")
                    continue

                cmd_count += 1
                handle_command(cf, addr, sock, control_port)

    except KeyboardInterrupt:
        pass
    finally:
        sock.close()
        admin_sock.close()
        log(f"\n{'=' * 70}")
        log(f"  Simulator stopped.")
        log(f"  Total raw UDP packets : {raw_count}")
        log(f"  Valid commands processed: {cmd_count}")
        log(f"{'=' * 70}")


if __name__ == '__main__':
    main()