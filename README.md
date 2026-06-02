# HyperScape100 Emulator

ICD-compliant emulator for the Simera Sense HyperScape100 hyperspectral imager. Streams session data over TCP with packet framing, CRC-32, and band reconfiguration, simulating the LVDS data interface and I2C/SPI control interface for development without hardware.

Includes an interactive TUI client for visualizing the data stream (see `client/README.md`).

## Architecture

```
┌─────────────────┐       TCP :4001 (data)       ┌─────────────────┐
│                 │ ──────────────────────────>  │                 │
│  emu            │                              │  your app       │
│  (server)       │ <──────────────────────────  │                 │
│                 │    TCP :4002 (control)       │                 │
└─────────────────┘                              └─────────────────┘
```

**Data port:** Streams a complete session binary in ICD packet format. One-way: emulator to client. Streams line-by-line from disk -- works with multi-GB scenes.

**Control port:** Accepts commands from the client:
- **RCFG** -- band reconfiguration (select which bands to stream)
- **NSCN** -- next/previous scene navigation

Ports are configurable via `--data-port` and `--control-port`.

## Quick start

### 1. Populate scene data

Download GeoTIFFs from the [Wyvern Open Data portal](https://opendata.wyvern.space/) and convert to raw format:

```bash
pip install -r tools/requirements.txt
python tools/convert.py path/to/scene.tiff -o data/
```

The conversion strips georeferencing, converts float32 radiance to 12-bit DN, and outputs raw line-by-line binary files. This ensures the emulator works with data as it would be produced by the camera hardware -- raw pixel values without ground-processing artifacts. It also keeps the emulator free of image library dependencies -- it reads simple binary with `fread`. The converter handles all source-specific details such as nodata values, band naming, and DN scaling once, so the emulator works the same regardless of where the data came from.

See `data/README.md` for the expected format and alternative sources.

### 2. Build and run

```bash
mkdir build && cd build && cmake .. && make
```

Start the emulator with a single scene or a directory of scenes:

```bash
./build/emu --scene data/my_scene
./build/emu --scene data/
./build/emu --scene data/ --data-port 5001 --control-port 5002
```

### 3. Connect a client

The included TUI client (see `client/README.md`):

```bash
cd client && cargo run --release
```

Or connect your own application to the TCP ports.

## Integrating with the emulator

To build software that consumes the HyperScape100 data stream:

1. **Connect** to the data port (default 4001) via TCP
2. **Read** the byte stream and scan for the sync word `0x5353` to find packet boundaries
3. **Parse** each packet's 8-byte header to get the packet ID and payload length
4. **Validate** the CRC-32 footer (4 bytes, zlib-compatible)
5. **Process** packets by ID -- the session follows this order:

```
SESSION START (0x00)
IMAGER INFORMATION (0xA0)
IMAGER CONFIGURATION (0xA1)  ← contains band wavelengths and count
TIME SYNCHRONISATION (0x07)
SCENE START (0x02)           ← contains scene width and height
  [IMAGER TELEMETRY (0xA3)]  ← every 500ms
  EXPOSURE START (0x03)      ← one per line
  LINE DATA (0x04)           ← one per band per line (pixel data here)
SESSION END (0x01)
```

6. **Reassemble** spectral data: each LINE DATA packet contains one band for one line. Collect all bands for the same line number to build per-pixel spectral vectors.

To **reconfigure bands**, connect to the control port (default 4002) and send:
```
"RCFG" (4 bytes) + n_bands (uint8) + wavelengths (n_bands × uint16 LE)
```
The emulator replies with `"ACK" + status (uint8, 0=ok)` and streams a new session.

To **switch scenes**, send:
```
"NSCN" (4 bytes) + direction (int8, +1=next, -1=prev)
```

The `src/packets/` library (C) provides ready-to-use packet parsing and serialization functions that can be linked into your application.

## Testing

```bash
cd build && ctest
```

94 tests across 8 suites covering packet format compliance, CRC-32, serialization round-trips, session structure, and TCP streaming.

## Project structure

```
src/
  packets/             ICD packet library (C)
  emu/                 Emulator server (C)
client/                Interactive TUI client (Rust, see client/README.md)
tests/                 94 TDD tests (C)
tools/                 GeoTIFF converter (Python, see tools/README.md)
data/                  Scene data (see data/README.md)
docs/                  Simera Sense ICDs, User Manual, CMV12000 datasheet
```

## ICD compliance

Verified against the xScape Control and Data ICD (doc 051715, rev 8). All packet layouts, struct sizes, field ordering, CRC-32 parameters, payload alignment, and session stream ordering match the specification.

## Transition to real hardware

Replace the TCP sockets with the real LVDS and I2C/SPI drivers. The packet format is identical -- only the transport layer changes.

## License

[MIT](LICENSE)
