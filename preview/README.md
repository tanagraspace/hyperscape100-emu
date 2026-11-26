# hs100-preview

The false-color scene preview of HyperScape100 line data, as a library crate. The TUI client in `../client` uses it, and other programs can use it too.

The preview builds line by line. Band 0 drives blue, the middle band drives green, and the last band drives red. A DN of 4095, the 12-bit maximum, maps to 255. The image keeps the scene's aspect and fits a maximum size.

The crate has no dependencies. `Preview::rgb` returns packed RGB rows, which an image library or a terminal renderer takes as they are.

```rust
use hs100_preview::{channel_for_band, Preview};

let mut preview = Preview::fitted(scene_width, scene_height, 800, 1200);
if let Some(channel) = channel_for_band(band, n_bands) {
    // the pixel bytes of a LINE DATA payload, little endian
    preview.put_band_line_le(line, channel, pixel_bytes);
}
```

## Use it from another repository

```toml
[dependencies]
hs100-preview = { git = "https://github.com/tanagraspace/hyperscape100-emu", rev = "<commit>" }
```

## Test

```bash
cargo test
```
