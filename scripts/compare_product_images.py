import argparse
import json

from validate_product_image import normalized_rmse, read_pfm, sha256


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True, type=__import__("pathlib").Path)
    parser.add_argument("--reference", required=True, type=__import__("pathlib").Path)
    parser.add_argument("--report", required=True, type=__import__("pathlib").Path)
    parser.add_argument("--max-relative-rmse", required=True, type=float)
    arguments = parser.parse_args()
    candidate_width, candidate_height, candidate = read_pfm(arguments.candidate)
    reference_width, reference_height, reference = read_pfm(arguments.reference)
    if (candidate_width, candidate_height) != (reference_width, reference_height):
        raise ValueError("transport images differ in dimensions")
    error = normalized_rmse(candidate, reference)
    if error > arguments.max_relative_rmse:
        raise ValueError(
            f"transport relative RGB NRMSE {error} exceeds "
            f"{arguments.max_relative_rmse}"
        )
    report = {
        "schema": "ure.preview.product-image-parity/1.0",
        "metric": "relative_rgb_nrmse",
        "candidate_sha256": sha256(arguments.candidate),
        "reference_sha256": sha256(arguments.reference),
        "relative_rgb_nrmse": error,
        "maximum_relative_rgb_nrmse": arguments.max_relative_rmse,
        "dimensions": [candidate_width, candidate_height],
        "byte_identical": arguments.candidate.read_bytes() == arguments.reference.read_bytes(),
    }
    arguments.report.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n"
    )
    print(f"transport image parity NRMSE {error:.6g}")


if __name__ == "__main__":
    main()
