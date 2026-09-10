import argparse
import hashlib
import json
import pathlib

from validate_product_image import VIEW_TRANSFORM, image_metrics, read_pfm, write_png


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--png", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    width, height, values = read_pfm(arguments.raw)
    metrics = image_metrics(width, height, values)
    write_png(arguments.png, width, height, values)
    report = {
        "schema": "ure.preview.measurement-image-validation/1.0",
        "raw_authority": {
            "semantic": "BeautyRaw",
            "format": "PFM RGB float32 little-endian",
            "orientation": "bottom-left",
            "color_space": "linear_sRGB_D65",
            "path": str(arguments.raw),
            "sha256": sha256(arguments.raw),
        },
        "derived_view": {
            "format": "PNG RGB8 sRGB",
            "orientation": "top-left",
            "view_transform": VIEW_TRANSFORM,
            "path": str(arguments.png),
            "sha256": sha256(arguments.png),
        },
        "metrics": metrics,
    }
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    print(f"validated {width}x{height} typed BeautyRaw: PNG {arguments.png}")


if __name__ == "__main__":
    main()
