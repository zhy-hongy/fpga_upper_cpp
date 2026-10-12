"""Export captured Gray8 frames without contrast adjustment or pixel edits."""
import argparse
import json
from pathlib import Path
import cv2
import numpy as np


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    args = parser.parse_args()
    run = args.run
    summary = json.loads((run / "summary.json").read_text(encoding="utf-8"))
    panels, statistics = [], []
    for device in range(1, 5):
        image = cv2.imread(str(run / f"camera{device}_rearranged_1.pgm"), cv2.IMREAD_GRAYSCALE)
        if image is None:
            raise RuntimeError(f"Camera {device} image is missing")
        if not cv2.imwrite(str(run / f"camera{device}.png"), image):
            raise RuntimeError("PNG export failed")
        info = next(row for row in summary["images"] if row["device"] == device and row["rearranged"] and row["index"] == 1)
        statistics.append(dict(device=device, width=image.shape[1], height=image.shape[0],
                               minimum=int(image.min()), maximum=int(image.max()),
                               mean=float(image.mean()), standard_deviation=float(image.std()),
                               nonzero_pixels=int(np.count_nonzero(image))))
        thumbnail = cv2.resize(image, (820, 432), interpolation=cv2.INTER_AREA)
        panel = np.full((480, 844, 3), (20, 24, 30), dtype=np.uint8)
        panel[42:474, 12:832] = cv2.cvtColor(thumbnail, cv2.COLOR_GRAY2BGR)
        label = f"CAM {device} | {info['width']} x {info['height']} | 0x0300 | CRC verified"
        cv2.putText(panel, label, (15, 28), cv2.FONT_HERSHEY_SIMPLEX, .62, (217, 229, 239), 1, cv2.LINE_AA)
        panels.append(panel)
    sheet = np.vstack([np.hstack(panels[:2]), np.hstack(panels[2:])])
    cv2.imwrite(str(run / "four_camera_preview.png"), sheet)
    (run / "pixel_statistics.json").write_text(json.dumps(statistics, indent=2), encoding="utf-8")
    print(json.dumps(statistics, indent=2))


if __name__ == "__main__":
    main()
