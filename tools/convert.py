#!/usr/bin/env python3
"""Convert hyperspectral GeoTIFFs to HyperScape100 raw on-board format.

Supports multiple data sources via pluggable adapters. Each adapter handles
source-specific details (nodata values, band naming conventions, scaling)
while the core converter produces the same output format.

Output format per scene:
  {scene_id}/
    lines/
      line_00000.bin    # width x n_bands x uint16 (12-bit DN in 16-bit container)
      line_00001.bin
      ...
    metadata.json       # band wavelengths, dimensions, scaling parameters
"""

import argparse
import json
import sys
from abc import ABC, abstractmethod
from pathlib import Path

import numpy as np
import rasterio

SENSOR_WIDTH = 4096
DN_MAX = 4095  # 12-bit


# ---------- Source adapters ----------


class SourceAdapter(ABC):
    """Base class for data source adapters."""

    @abstractmethod
    def nodata_value(self):
        """Return the nodata sentinel value for this source."""

    @abstractmethod
    def extract_wavelengths(self, src):
        """Extract band centre wavelengths (nm) from a rasterio dataset."""

    @abstractmethod
    def find_tiffs(self, paths):
        """Find GeoTIFF files from input paths."""

    @abstractmethod
    def scene_id(self, tiff_path):
        """Derive a scene ID from a GeoTIFF path."""


class WyvernAdapter(SourceAdapter):
    """Adapter for Wyvern Open Data Program GeoTIFFs.

    Wyvern's Dragonette constellation uses the same HyperScape100 sensor.
    Data is L1B radiance in float32 with nodata=-9999 and band descriptions
    like 'Band_445', 'Band_464', etc.
    """

    def nodata_value(self):
        return -9999.0

    def extract_wavelengths(self, src):
        wavelengths = []
        for i in range(src.count):
            desc = src.descriptions[i] or f"Band_{i}"
            try:
                nm = int(desc.replace("Band_", ""))
            except ValueError:
                nm = i
            wavelengths.append(nm)
        return wavelengths

    def find_tiffs(self, paths):
        tiffs = []
        for p in paths:
            if p.is_dir():
                tiffs.extend(sorted(p.glob("**/*.tiff")))
                tiffs.extend(sorted(p.glob("**/*.tif")))
            elif p.suffix.lower() in (".tif", ".tiff"):
                tiffs.append(p)
        return tiffs

    def scene_id(self, tiff_path):
        return tiff_path.stem


# ---------- Adapter registry ----------


ADAPTERS = {
    "wyvern": WyvernAdapter,
}


# ---------- Core converter ----------


def radiance_to_dn(radiance, r_min, r_max):
    dn = np.zeros_like(radiance, dtype=np.uint16)
    valid = radiance != 0
    if r_max > r_min:
        scaled = (radiance[valid] - r_min) / (r_max - r_min) * DN_MAX
        dn[valid] = np.clip(scaled, 0, DN_MAX).astype(np.uint16)
    return dn


def convert_scene(tiff_path, output_dir, adapter, center_crop=True):
    tiff_path = Path(tiff_path)
    scene_name = adapter.scene_id(tiff_path)
    nodata = adapter.nodata_value()

    with rasterio.open(tiff_path) as src:
        n_bands = src.count
        height = src.height
        width = src.width
        wavelengths = adapter.extract_wavelengths(src)

        print(f"  Input: {width}x{height} px, {n_bands} bands, {src.dtypes[0]}")
        print(f"  Bands: {wavelengths[0]}-{wavelengths[-1]} nm")

        # Extract geolocation
        bounds = src.bounds
        crs = str(src.crs) if src.crs else None
        geo = {
            "crs": crs,
            "bbox": [bounds.left, bounds.bottom, bounds.right, bounds.top],
            "corners": {
                "top_left": {"lon": bounds.left, "lat": bounds.top},
                "top_right": {"lon": bounds.right, "lat": bounds.top},
                "bottom_left": {"lon": bounds.left, "lat": bounds.bottom},
                "bottom_right": {"lon": bounds.right, "lat": bounds.bottom},
            },
        }
        if crs:
            print(f"  Bbox: {bounds.bottom:.4f},{bounds.left:.4f} to {bounds.top:.4f},{bounds.right:.4f}")

        all_bands = src.read()

        r_mins = np.zeros(n_bands)
        r_maxs = np.zeros(n_bands)
        for b in range(n_bands):
            valid = all_bands[b][all_bands[b] != nodata]
            if len(valid) > 0:
                r_mins[b] = np.percentile(valid, 1)
                r_maxs[b] = np.percentile(valid, 99)
            else:
                r_mins[b] = 0
                r_maxs[b] = 1

    # mark nodata pixels as zero before scaling
    all_bands[all_bands == nodata] = 0

    if center_crop and width > SENSOR_WIDTH:
        x_start = (width - SENSOR_WIDTH) // 2
        all_bands = all_bands[:, :, x_start : x_start + SENSOR_WIDTH]
        out_width = SENSOR_WIDTH
        print(f"  Center-cropped to {SENSOR_WIDTH} px width")
    elif width < SENSOR_WIDTH:
        pad_left = (SENSOR_WIDTH - width) // 2
        pad_right = SENSOR_WIDTH - width - pad_left
        all_bands = np.pad(
            all_bands,
            ((0, 0), (0, 0), (pad_left, pad_right)),
            constant_values=0,
        )
        out_width = SENSOR_WIDTH
        print(f"  Padded to {SENSOR_WIDTH} px width")
    else:
        out_width = width

    dn_bands = np.zeros((n_bands, height, out_width), dtype=np.uint16)
    for b in range(n_bands):
        dn_bands[b] = radiance_to_dn(all_bands[b], r_mins[b], r_maxs[b])

    scene_dir = output_dir / scene_name
    lines_dir = scene_dir / "lines"
    lines_dir.mkdir(parents=True, exist_ok=True)

    valid_lines = 0
    for y in range(height):
        line = dn_bands[:, y, :]
        if line.max() == 0:
            continue
        line_pil = line.T
        line_path = lines_dir / f"line_{valid_lines:05d}.bin"
        line_pil.tofile(line_path)
        valid_lines += 1

    metadata = {
        "scene_id": scene_name,
        "source": type(adapter).__name__,
        "source_tiff": tiff_path.name,
        "n_bands": n_bands,
        "wavelengths_nm": wavelengths,
        "width_px": out_width,
        "n_lines": valid_lines,
        "original_width": width,
        "original_height": height,
        "geo": geo,
        "dtype": "uint16",
        "dn_bits": 12,
        "dn_max": DN_MAX,
        "pixel_layout": "BIL (band-interleaved-by-line: width x n_bands per line file)",
        "radiance_to_dn_scaling": {
            "method": "linear_percentile",
            "percentile_low": 1,
            "percentile_high": 99,
            "per_band_min": r_mins.tolist(),
            "per_band_max": r_maxs.tolist(),
        },
    }

    with open(scene_dir / "metadata.json", "w") as f:
        json.dump(metadata, f, indent=2)

    total_bytes = valid_lines * out_width * n_bands * 2
    print(f"  Output: {valid_lines} lines, {out_width}x{n_bands} per line, {total_bytes / 1e6:.1f} MB")
    print(f"  Saved to {scene_dir}/")

    return metadata


def main():
    parser = argparse.ArgumentParser(
        description="Convert hyperspectral GeoTIFFs to HyperScape100 raw format"
    )
    parser.add_argument(
        "input",
        type=Path,
        nargs="+",
        help="Input GeoTIFF file(s) or directory containing them",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("../data"),
        help="Output directory (default: ../data/)",
    )
    parser.add_argument(
        "--source",
        choices=list(ADAPTERS.keys()),
        default="wyvern",
        help="Data source adapter (default: wyvern)",
    )
    parser.add_argument(
        "--no-crop",
        action="store_true",
        help="Don't center-crop to 4096 px sensor width",
    )

    args = parser.parse_args()

    adapter = ADAPTERS[args.source]()

    tiffs = adapter.find_tiffs(args.input)
    if not tiffs:
        print("No GeoTIFFs found", file=sys.stderr)
        sys.exit(1)

    print(f"Source: {args.source}")
    print(f"Converting {len(tiffs)} scenes to raw on-board format")
    print(f"Output: {args.output}/")
    print()

    for i, tiff in enumerate(tiffs, 1):
        print(f"[{i}/{len(tiffs)}] {tiff.name}")
        convert_scene(tiff, args.output, adapter, center_crop=not args.no_crop)
        print()


if __name__ == "__main__":
    main()
