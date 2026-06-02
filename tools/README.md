# Tools

## convert.py

Converts hyperspectral GeoTIFFs to the raw line-by-line format expected by the emulator. Supports multiple data sources via pluggable adapters.

### Supported sources

| Source | Adapter | Description |
|--------|---------|-------------|
| `wyvern` (default) | `WyvernAdapter` | Wyvern Open Data Program GeoTIFFs |

To add a new source, subclass `SourceAdapter` in `convert.py` and register it in the `ADAPTERS` dict. Each adapter defines how to handle nodata values, extract band wavelengths, find files, and derive scene IDs.

### Why Wyvern data?

The [Wyvern Dragonette constellation](https://wyvern.space/) uses the Simera Sense HyperScape100 as its core satellite camera ([source](https://simera-sense.com/news/hyperspectral-the-new-black/)). Their [Open Data Program](https://wyvern.space/open-data/) provides free hyperspectral imagery under CC BY 4.0 -- this is real on-orbit data from the same sensor the emulator targets. Using Wyvern data means the emulator streams spectrally accurate imagery that matches what the HyperScape100 actually produces in orbit, not a synthetic proxy from a different instrument.

Dragonette-002 and Dragonette-003 provide 31 bands across 445-880 nm at 5.3 m GSD.

### Why convert?

GeoTIFFs from supported sources are ground-processed products: float32 radiance values with georeferencing and map projection. The HyperScape100 on-board data is raw 12-bit DN values in line-by-line packets without georeferencing. The conversion ensures the emulator works with data as it would be produced by the camera hardware, not ground-processed artifacts.

It also keeps the emulator itself free of image library dependencies -- it reads simple binary files, no GDAL or libgeotiff required. Source-specific details like nodata values, band naming conventions, and DN scaling are handled once during conversion, so the emulator works the same regardless of where the data came from.

### What the converter does

This script reverses the ground processing:
- Strips georeferencing
- Converts float32 radiance to 12-bit unsigned integer DN (0-4095)
- Center-crops or pads to 4096 px sensor width
- Outputs line-by-line binary files
- Skips nodata lines

### Setup

```bash
pip install -r requirements.txt
```

### Usage

Convert using the default source (Wyvern):

```bash
python convert.py path/to/scene.tiff -o ../data/
```

Convert all GeoTIFFs in a directory:

```bash
python convert.py path/to/geotiffs/ -o ../data/
```

Specify a source explicitly:

```bash
python convert.py path/to/scene.tiff -o ../data/ --source wyvern
```

Keep original width (don't crop to 4096 px):

```bash
python convert.py path/to/scene.tiff -o ../data/ --no-crop
```

### Adding a new source

```python
class MySourceAdapter(SourceAdapter):
    def nodata_value(self):
        return -9999.0  # or whatever your source uses

    def extract_wavelengths(self, src):
        # parse band wavelengths from rasterio dataset metadata
        ...

    def find_tiffs(self, paths):
        # find GeoTIFF files from input paths
        ...

    def scene_id(self, tiff_path):
        # derive a scene name from the file path
        ...

ADAPTERS["mysource"] = MySourceAdapter
```

### DN scaling

The float32 radiance is scaled to 12-bit DN using per-band linear scaling with 1st/99th percentile clipping. The exact scaling parameters are saved in each scene's `metadata.json` for reference.
