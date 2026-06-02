# Scene Data

This directory holds scene data for the emulator. Each scene is a subdirectory. The emulator can load a single scene or scan this entire directory for all available scenes.

## Format

```
data/
  scene_a/
    metadata.json
    lines/
      line_00000.bin
      line_00001.bin
      ...
  scene_b/
    metadata.json
    lines/
      ...
```

### metadata.json

```json
{
  "n_bands": 31,
  "width_px": 4096,
  "n_lines": 8936,
  "wavelengths_nm": [445, 464, 480, 490, ...]
}
```

### Line files

Each `line_NNNNN.bin` is `width_px × n_bands` uint16 values, pixel-interleaved (all bands for each pixel are contiguous). Values are 12-bit DN (0-4095) in little-endian uint16.

## Populating

### From Wyvern Open Data (recommended)

Download GeoTIFFs from the [Wyvern Open Data portal](https://opendata.wyvern.space/) and convert:

```bash
pip install -r tools/requirements.txt
python tools/convert.py path/to/scene.tiff -o data/
```

Dragonette-002 and Dragonette-003 scenes use the same HyperScape100 sensor (31 bands, 445-880 nm). See `tools/README.md` for details.

### Synthetic test data

```python
import numpy as np, json, os

n_bands, width, n_lines = 8, 256, 100
wavelengths = [450, 520, 580, 640, 700, 750, 800, 860]
os.makedirs("data/synthetic/lines", exist_ok=True)

for line in range(n_lines):
    np.random.randint(0, 4096, (width, n_bands), dtype=np.uint16).tofile(
        f"data/synthetic/lines/line_{line:05d}.bin")

json.dump({"n_bands": n_bands, "width_px": width, "n_lines": n_lines,
           "wavelengths_nm": wavelengths},
          open("data/synthetic/metadata.json", "w"), indent=2)
```

## Note

Scene data is not tracked in git.
