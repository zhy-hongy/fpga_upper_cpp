"""Optional offline differential test using an existing Python upper-computer tree.

Run with that application's Python environment (NumPy/OpenCV required):
  python data_reference_compare.py --python-root D:/CODE2/fpga_upper --probe path/to/fpga_data_test.exe
It never connects to a device or modifies captured frames.
"""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import zlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--python-root", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.python_root))
    import numpy as np
    from protocol.control_frame import parse_camera_data_response
    from backend.sf.sf_fpga_parser import parse_0x0300_blocks

    captures = sorted((args.python_root / "raw_data").glob("raw_0300_full_*.bin"))
    if not captures:
        raise RuntimeError("no offline 0x0300 captures available")
    for path in captures:
        frame = path.read_bytes()
        command = int.from_bytes(frame[4:6], "big")
        blocks = parse_camera_data_response(frame[14:-4], rearranged=command == 0x0300)
        result = parse_0x0300_blocks(blocks["blocks"], as_arrays=True, image_layout="declared")
        crc = lambda array: zlib.crc32(np.asarray(array, dtype=">f8").tobytes()) & 0xffffffff
        expected = dict(width=result.image.shape[1], height=result.image.shape[0],
                        image_crc32=zlib.crc32(result.image.tobytes()) & 0xffffffff,
                        center_count=len(result.centerline), center_crc32=crc(result.centerline),
                        cloud_count=len(result.points3d), cloud_crc32=crc(result.points3d),
                        interp_count=len(result.interpolated), interp_crc32=crc(result.interpolated),
                        x_start=result.interp_x_start, x_end=result.interp_x_end,
                        interval=result.interp_x_interval,
                        height_corrected=any(b["parsed"].get("dimension_corrected", False) for b in blocks["blocks"]))
        actual = json.loads(subprocess.check_output([str(args.probe.resolve()), "--probe", str(path)], text=True))
        if actual != expected:
            raise AssertionError(f"{path.name}: C++ {actual} != Python {expected}")
        print(f"PASS {path.name}: image and all center/cloud/interpolation values match")
    print(f"{len(captures)} captured frames passed Python/C++ differential validation")


if __name__ == "__main__":
    main()
