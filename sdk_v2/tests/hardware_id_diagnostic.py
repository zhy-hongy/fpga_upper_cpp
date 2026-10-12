"""Read-only raw UDP diagnostics against the actual Python wire implementation."""
import argparse
import json
from pathlib import Path
import socket
import struct
import sys
import time


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--python-root", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    args = p.parse_args()
    sys.path.insert(0, str(args.python_root))
    from protocol.control_frame import pack_cmd_frame, unpack_cmd_frame
    from protocol.transport_frame import TransportAssembler, split_into_transport_frames
    from protocol.crc16 import set_crc_verify, crc16_modbus
    set_crc_verify(True)
    args.output.mkdir(exist_ok=False)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 128 << 20)
    sock.bind(("192.168.10.100", 60000))
    sock.settimeout(.02)
    rows = []
    with (args.output / "raw_datagrams.bin").open("wb") as raw:
        try:
            for command, ids in [(1, [0x1001, 0x7ff1, 0x8001, 0xdff1, 0xfff1]),
                                 (0x300, [0x1002, 0x7ff2, 0x8002, 0xdff2, 0xfff2])]:
                for mid in ids:
                    command_bytes = pack_cmd_frame(1, command, mid, msg_type=0, rw=0x80, data=b"\0\0")
                    # Same layout as the SDK; frame and transport IDs are identical.
                    packets = split_into_transport_frames(command_bytes, packet_id=mid, device_type=1)
                    row = dict(command=command, message_id=mid, packets=0, errors=[], frames=[], matched=False)
                    assembler = TransportAssembler()
                    for packet in packets:
                        sock.sendto(packet, ("192.168.10.10", 60000))
                    end = time.monotonic() + 1.5
                    while time.monotonic() < end:
                        try:
                            data, source = sock.recvfrom(65535)
                        except socket.timeout:
                            continue
                        raw.write(struct.pack(">I", len(data)) + data)
                        if source != ("192.168.10.10", 60000):
                            continue
                        row["packets"] += 1
                        def failed(packet, error):
                            if len(row["errors"]) < 8:
                                row["errors"].append(dict(error=str(error), size=len(packet), head=packet[:40].hex(), tail=packet[-8:].hex()))
                        _, frame = assembler.feed(data, on_error=failed)
                        if not frame:
                            continue
                        try:
                            decoded = unpack_cmd_frame(frame)
                            received_crc = int.from_bytes(frame[-4:-2], "big")
                            computed_crc = crc16_modbus(frame[2:-4])
                            info = dict(device=decoded["device_type"], command=decoded["cmd_code"],
                                        message_id=decoded["msg_id"], rw=decoded["rw"], type=decoded["msg_type"],
                                        data_length=len(decoded["data"]), crc_ok=received_crc == computed_crc)
                            row["frames"].append(info)
                            if (info["device"], info["command"], info["message_id"], info["type"], info["rw"]) == (1, command, mid, 1, 0x80) and info["crc_ok"]:
                                row["matched"] = True
                                if command == 0x300:
                                    (args.output / f"image_mid_{mid:04x}.bin").write_bytes(frame)
                                break
                        except ValueError as error:
                            row["errors"].append(dict(error=str(error), size=len(frame), head=frame[:40].hex(), tail=frame[-8:].hex()))
                    rows.append(row)
                    print(json.dumps(row), flush=True)
                    time.sleep(.05)
        finally:
            sock.close()
    (args.output / "id_diagnostic.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
