import argparse
import array
import hashlib
import json
import math
import pathlib
import struct
import sys
import zlib


VIEW_TRANSFORM = "ure.preview.view.linear-srgb-reinhard-srgb8/1.0"


def read_line(stream):
    value = stream.readline()
    if not value or len(value) > 128:
        raise ValueError("PFM header is missing or oversized")
    return value.decode("ascii").strip()


def read_pfm(path):
    with path.open("rb") as stream:
        if read_line(stream) != "PF":
            raise ValueError(f"{path} is not a color PFM")
        dimensions = read_line(stream).split()
        if len(dimensions) != 2:
            raise ValueError(f"{path} has invalid dimensions")
        width, height = (int(value) for value in dimensions)
        scale = float(read_line(stream))
        if width <= 0 or height <= 0 or scale == 0:
            raise ValueError(f"{path} has invalid shape or scale")
        payload = stream.read()
    expected = width * height * 3 * 4
    if len(payload) != expected:
        raise ValueError(f"{path} payload size is {len(payload)}, expected {expected}")
    values = array.array("f")
    values.frombytes(payload)
    little_endian = scale < 0
    if little_endian != (sys.byteorder == "little"):
        values.byteswap()
    row_values = width * 3
    top_down = array.array("f")
    for row in range(height - 1, -1, -1):
        begin = row * row_values
        top_down.extend(values[begin : begin + row_values])
    return width, height, top_down


def image_metrics(width, height, values):
    count = width * height
    luminance = array.array("d")
    nonzero = 0
    negative = 0
    maximum = -math.inf
    minimum = math.inf
    energy = 0.0
    for offset in range(0, len(values), 3):
        red = float(values[offset])
        green = float(values[offset + 1])
        blue = float(values[offset + 2])
        if not math.isfinite(red) or not math.isfinite(green) or not math.isfinite(blue):
            raise ValueError("PFM contains a non-finite sample")
        minimum = min(minimum, red, green, blue)
        maximum = max(maximum, red, green, blue)
        negative += int(red < 0) + int(green < 0) + int(blue < 0)
        nonzero += int(max(abs(red), abs(green), abs(blue)) > 1.0e-7)
        value = 0.2126 * red + 0.7152 * green + 0.0722 * blue
        luminance.append(value)
        energy += max(value, 0.0)
    mean = sum(luminance) / count
    variance = sum((value - mean) ** 2 for value in luminance) / count
    gradient_sum = 0.0
    gradient_count = 0
    for y in range(height):
        row = y * width
        for x in range(width):
            index = row + x
            if x + 1 < width:
                gradient_sum += abs(luminance[index + 1] - luminance[index])
                gradient_count += 1
            if y + 1 < height:
                gradient_sum += abs(luminance[index + width] - luminance[index])
                gradient_count += 1
    result = {
        "width": width,
        "height": height,
        "finite": True,
        "minimum": minimum,
        "maximum": maximum,
        "mean_luminance": mean,
        "positive_energy": energy,
        "luminance_standard_deviation": math.sqrt(variance),
        "mean_spatial_gradient": gradient_sum / max(gradient_count, 1),
        "nonzero_pixel_fraction": nonzero / count,
        "negative_component_fraction": negative / len(values),
    }
    if not 1.0e-8 < result["mean_luminance"] < 1.0e4:
        raise ValueError("image mean luminance is outside the product evidence bounds")
    if result["maximum"] >= 1.0e6:
        raise ValueError("image radiance exceeds the product evidence bound")
    if result["nonzero_pixel_fraction"] <= 0.002:
        raise ValueError("image is trivial or empty")
    if result["luminance_standard_deviation"] <= 1.0e-5:
        raise ValueError("image has no measurable spatial structure")
    if result["mean_spatial_gradient"] <= 1.0e-6:
        raise ValueError("image has no measurable edge structure")
    return result


def normalized_rmse(candidate, reference):
    if len(candidate) != len(reference):
        raise ValueError("convergence images differ in shape")
    squared_error = 0.0
    squared_reference = 0.0
    for left, right in zip(candidate, reference):
        difference = float(left) - float(right)
        squared_error += difference * difference
        squared_reference += float(right) * float(right)
    return math.sqrt(squared_error / max(squared_reference, 1.0e-30))


def srgb(value):
    if value <= 0.0031308:
        return 12.92 * value
    return 1.055 * value ** (1.0 / 2.4) - 0.055


def png_chunk(kind, payload):
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))


def write_png(path, width, height, values):
    scanlines = bytearray()
    row_values = width * 3
    for y in range(height):
        scanlines.append(0)
        begin = y * row_values
        for value in values[begin : begin + row_values]:
            radiance = max(float(value), 0.0)
            mapped = radiance / (1.0 + radiance)
            encoded = min(max(srgb(mapped), 0.0), 1.0)
            scanlines.append(round(encoded * 255.0))
    payload = bytearray(b"\x89PNG\r\n\x1a\n")
    payload.extend(png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
    payload.extend(png_chunk(b"sRGB", b"\x00"))
    payload.extend(png_chunk(b"tEXt", b"UltraRenderView\x00" + VIEW_TRANSFORM.encode("ascii")))
    payload.extend(png_chunk(b"IDAT", zlib.compress(bytes(scanlines), 9)))
    payload.extend(png_chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--low", required=True, type=pathlib.Path)
    parser.add_argument("--mid", required=True, type=pathlib.Path)
    parser.add_argument("--final", required=True, type=pathlib.Path)
    parser.add_argument("--png", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    low_width, low_height, low = read_pfm(arguments.low)
    mid_width, mid_height, mid = read_pfm(arguments.mid)
    width, height, final = read_pfm(arguments.final)
    if (low_width, low_height) != (width, height) or (mid_width, mid_height) != (width, height):
        raise ValueError("progressive and final artifacts differ in dimensions")
    low_error = normalized_rmse(low, final)
    mid_error = normalized_rmse(mid, final)
    if not mid_error < low_error:
        raise ValueError("nested progressive estimate did not move toward the final estimate")
    metrics = image_metrics(width, height, final)
    write_png(arguments.png, width, height, final)
    report = {
        "schema": "ure.preview.product-image-validation/1.0",
        "raw_authority": {
            "format": "PFM RGB float32 little-endian",
            "orientation": "bottom-left",
            "color_space": "linear_sRGB_D65",
            "final": str(arguments.final),
            "final_sha256": sha256(arguments.final),
            "low_sha256": sha256(arguments.low),
            "mid_sha256": sha256(arguments.mid),
        },
        "derived_view": {
            "format": "PNG RGB8 sRGB",
            "orientation": "top-left",
            "view_transform": VIEW_TRANSFORM,
            "path": str(arguments.png),
            "sha256": sha256(arguments.png),
        },
        "metrics": metrics,
        "convergence": {
            "metric": "relative_rgb_nrmse_to_nested_final",
            "low_to_final": low_error,
            "mid_to_final": mid_error,
            "improvement_ratio": mid_error / low_error,
        },
    }
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"validated {width}x{height}: mid/final NRMSE {mid_error:.6g}, PNG {arguments.png}")


if __name__ == "__main__":
    main()
