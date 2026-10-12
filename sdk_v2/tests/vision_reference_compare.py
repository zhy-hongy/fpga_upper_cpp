"""Compare optional C++ vision detectors with the actual Python upper computer."""
from __future__ import annotations
import argparse
import importlib
import math
from pathlib import Path
import subprocess
import sys
import tempfile

def main() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument("--python-root",required=True,type=Path)
    parser.add_argument("--vision-test",required=True,type=Path)
    args=parser.parse_args()
    sys.path.insert(0,str(args.python_root.resolve()))
    import cv2
    laser=importlib.import_module("backend.laser_line_detector").LaserLineDetector()
    cross=importlib.import_module("backend.alignment_detector").AlignmentDetector()
    Chessboard=importlib.import_module("backend.chessboard_detector").ChessboardDetector
    with tempfile.TemporaryDirectory(prefix="fpga_vision_diff_") as directory:
        generated=subprocess.run([str(args.vision_test.resolve()),"--reference",directory],
                                 check=True,capture_output=True,text=True)
        count=0
        for line in generated.stdout.splitlines():
            fields=line.split();kind=fields[0]
            image=cv2.imread(str(Path(directory)/fields[1]),cv2.IMREAD_GRAYSCALE)
            assert image is not None
            if kind=="L":
                result=laser.detect(image)
                assert result.status==fields[2]
                actual=[result.angle_deg,result.distance_px,result.inlier_count,result.total_count,
                        result.valid_ratio,result.quality]
                expected=list(map(float,fields[3:]))
                tolerance=0.0001
            elif kind=="C":
                result=cross.detect(image)
                assert result.status==fields[2] and result.center is not None
                actual=[*result.center,result.offset_x,result.offset_y,result.rotation,result.quality,
                        len(result.horizontal.points),len(result.vertical.points)]
                expected=list(map(float,fields[3:]))
                tolerance=0.0001
            elif kind=="B":
                detector=Chessboard((int(fields[2]),int(fields[3])))
                result=detector.detect(image)
                assert result.found==bool(int(fields[4]))
                actual=[len(result.corners),result.avg_distance,result.min_distance,result.max_distance,result.variance]
                expected=list(map(float,fields[5:]))
                tolerance=0.001
            else:
                raise AssertionError(f"unknown vision reference {kind}")
            assert len(actual)==len(expected)
            for index,(python,cpp) in enumerate(zip(actual,expected)):
                assert math.isclose(python,cpp,abs_tol=tolerance,rel_tol=1e-6), (
                    f"{kind} {fields[1]} statistic {index} differs: Python={python}, C++={cpp}")
            count+=1
        print(f"Vision Python/C++ differential passed: {count} images, "
              "laser angles/centers, alignment full/local ROI, chessboard small/large patterns and downscaling")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
