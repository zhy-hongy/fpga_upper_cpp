"""Optional DLL/focus/SF comparison differential checks against Python upper computer."""

import argparse
import json
import math
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
    from backend.sf.sf_compare import SfCompareEngine
    from backend.sf.sf_types import SfAlgorithmResult
    from backend.focus_detector import FocusDetector
    from fpga_sf_test.laser_profiler_dll import LaserProfiler

    algorithm_root = args.python_root / "fpga_sf_test"
    dll = algorithm_root / "dll/laser_profiler_dll_v2_999.dll"
    kernel = algorithm_root / "data/kernel.xml"
    net = algorithm_root / "data/neural_net.xml"
    crc = lambda array: zlib.crc32(np.asarray(array, dtype=">f8").tobytes()) & 0xffffffff
    fields = ("status", "count", "mean_error", "rms_error", "max_error", "p95_error", "signed_bias",
              "dll_valid_count", "fpga_valid_count", "dll_only_valid_count", "fpga_only_valid_count", "both_invalid_count")
    captures = sorted((args.python_root / "raw_data").glob("raw_0300_full_*.bin"))
    if not captures:
        raise RuntimeError("no captured frames available")
    for path in captures:
        frame = path.read_bytes()
        parsed = parse_camera_data_response(frame[14:-4], rearranged=int.from_bytes(frame[4:6], "big") == 0x0300)
        fpga = parse_0x0300_blocks(parsed["blocks"], as_arrays=True, image_layout="declared")
        focus = FocusDetector()
        first, second = focus.detect(fpga.image, None), focus.detect(fpga.image, (307, 202, 951, 213))
        expected_focus = dict(full_score=first.score, full_mean=first.mean_gray, full_saturation=first.saturation,
                              smoothed_roi_score=second.score, roi_mean=second.mean_gray, roi_saturation=second.saturation)
        actual_focus = json.loads(subprocess.check_output([str(args.probe.resolve()), "--focus", str(path)], text=True))
        for name, expected in expected_focus.items():
            if not math.isclose(actual_focus[name], expected, rel_tol=1e-12, abs_tol=1e-12):
                raise AssertionError(f"{path.name}: focus {name}: C++={actual_focus[name]}, Python={expected}")
        profiler = LaserProfiler(str(dll))
        if not profiler.init(str(kernel), str(net)):
            raise RuntimeError("reference DLL initialization failed")
        profiler.set_extraction_config()
        profiler.set_interpolation_config(num_points=1600, x_interval=0.0625, x_start=-50, x_end=49.9375)
        try:
            with profiler.process(fpga.image) as native:
                if not native.success:
                    raise RuntimeError(native.error_msg)
                expected = dict(center_count=len(native.center_points), center_crc32=crc(native.center_points),
                                gray_crc32=zlib.crc32(native.center_grays.tobytes()) & 0xffffffff,
                                cloud_count=len(native.original_points), cloud_crc32=crc(native.original_points),
                                interp_count=len(native.interpolated_points), interp_crc32=crc(native.interpolated_points))
                comparison = SfCompareEngine().compare(SfAlgorithmResult(native.center_points, native.original_points,
                            native.interpolated_points, native.interpolated_points[:, 0].copy(), 0, len(native.center_points),
                            len(native.original_points)), fpga)
                expected_comparison = dict(center=comparison.center_stats, pointcloud=comparison.pointcloud_stats,
                                           interpolation=comparison.interp_stats)
        finally:
            profiler.destroy()
        actual = json.loads(subprocess.check_output([str(args.probe.resolve()), "--native", str(dll), str(kernel), str(net), str(path)], text=True))
        actual_comparison = actual.pop("comparison")
        if actual != expected:
            raise AssertionError(f"{path.name}: C++ native {actual} != Python native {expected}")
        for stage, stats in expected_comparison.items():
            for field in fields:
                a, b = actual_comparison[stage][field], getattr(stats, field)
                if isinstance(b, float):
                    match = math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-12)
                else:
                    match = a == b
                if not match:
                    raise AssertionError(f"{path.name}: comparison {stage}.{field}: C++={a}, Python={b}")
        print(f"PASS {path.name}: DLL result values, focus and SF comparison statistics match Python")
    print(f"{len(captures)} captured frames passed native/focus/SF differential validation")


if __name__ == "__main__":
    main()
