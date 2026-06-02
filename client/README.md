# hs100-client

Interactive terminal client for the HyperScape100 emulator. Connects to the emulator's TCP data and control ports, receives the session stream, parses ICD packets in real time, and renders a false-color scene preview directly in the terminal.

## Features

- Live packet parsing with stats (packet counts, CRC validation, data rate)
- False-color scene preview building line-by-line (R: NIR, G: mid-visible, B: blue)
- Zoom, pan, and rotate the scene preview
- Progress bar showing capture progress
- Band selector: toggle individual spectral bands on/off with scrolling list
- Scene navigation: cycle through multiple scenes loaded by the emulator
- Scene/band controls blocked during streaming to prevent state corruption

## Requirements

- Rust toolchain
- A terminal with Sixel or Kitty graphics support for image preview (iTerm2, Kitty, WezTerm, foot). Falls back to halfblocks on unsupported terminals.
- The emulator running (see root README)

## Build

```bash
cargo build --release
```

## Usage

```bash
# Connect to emulator on default ports
cargo run --release

# Specify host and ports
cargo run --release -- --host localhost --data-port 4001 --control-port 4002
```

## Keyboard

### Normal mode

| Key | Action |
|-----|--------|
| **B** | Open band selector |
| **P** / **N** | Previous / next scene |
| **+** / **-** | Zoom in / out |
| **Arrow keys** | Pan (when zoomed in) |
| **<** / **>** | Rotate counter-clockwise / clockwise (10° steps) |
| **0** | Reset zoom, pan, and rotation |
| **Q** | Quit |

Scene/band controls (B, P, N) are disabled while a session is streaming.

### Band selector

| Key | Action |
|-----|--------|
| **↑↓** | Navigate bands |
| **Space** | Toggle band on/off |
| **A** | Toggle all bands |
| **Enter** | Apply selection and stream new session |
| **Esc** | Cancel |

## Scene preview

The preview shows a false-color RGB composite built from three bands:
- **Red channel**: last band (NIR, ~870 nm)
- **Green channel**: middle band (~650 nm)
- **Blue channel**: first band (~445 nm)

The image builds line-by-line as packets arrive, scaled to fit the terminal. The scene title shows the current zoom level and rotation angle when active.
