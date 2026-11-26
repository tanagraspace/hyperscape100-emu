//! False-color scene preview of HyperScape100 line data.
//!
//! The preview builds line by line as LINE DATA arrives. Three bands
//! drive the color: band 0 drives blue, the middle band drives green,
//! and the last band drives red. A DN of 4095, the 12-bit maximum,
//! maps to 255. The image keeps the scene's aspect and fits a maximum
//! size, so a scene line maps to one image row and the columns are
//! sampled.
//!
//! The crate has no dependencies. `Preview::rgb` gives the pixels as
//! packed RGB rows, which an image or terminal library can take as is.

/// The color channel that a band drives.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Channel {
    Red,
    Green,
    Blue,
}

impl Channel {
    fn offset(self) -> usize {
        match self {
            Channel::Red => 0,
            Channel::Green => 1,
            Channel::Blue => 2,
        }
    }
}

/// The largest DN of the imager's 12-bit pixels.
pub const DN_MAX: u16 = 4095;

/// The image size that fits a scene into `max_w` by `max_h` and keeps
/// its aspect. No side is below 1.
pub fn fit(scene_w: u32, scene_h: u32, max_w: u32, max_h: u32) -> (u32, u32) {
    let aspect = scene_w as f64 / scene_h.max(1) as f64;
    let (w, h) = if aspect > max_w as f64 / max_h.max(1) as f64 {
        (max_w, (max_w as f64 / aspect) as u32)
    } else {
        ((max_h as f64 * aspect) as u32, max_h)
    };
    (w.max(1), h.max(1))
}

/// The channel of band `band` in a session of `n_bands` bands, or None
/// for a band that drives no channel. Band 0 takes precedence, then
/// the middle band.
pub fn channel_for_band(band: u8, n_bands: u8) -> Option<Channel> {
    if n_bands == 0 || band >= n_bands {
        None
    } else if band == 0 {
        Some(Channel::Blue)
    } else if band == n_bands / 2 {
        Some(Channel::Green)
    } else if band == n_bands - 1 {
        Some(Channel::Red)
    } else {
        None
    }
}

/// A DN scaled to 8 bits. A DN above 4095 saturates.
pub fn dn_to_u8(dn: u16) -> u8 {
    ((dn.min(DN_MAX) as f32 / DN_MAX as f32) * 255.0) as u8
}

/// The preview image of one scene.
#[derive(Debug, Clone)]
pub struct Preview {
    scene_w: u32,
    scene_h: u32,
    width: u32,
    height: u32,
    rgb: Vec<u8>,
    written: Vec<bool>,
}

impl Preview {
    /// A black image of `width` by `height` for a scene of `scene_w`
    /// pixels by `scene_h` lines. `fit` gives a size that keeps the
    /// scene's aspect.
    pub fn new(scene_w: u32, scene_h: u32, width: u32, height: u32) -> Self {
        let (width, height) = (width.max(1), height.max(1));
        Preview {
            scene_w,
            scene_h,
            width,
            height,
            rgb: vec![0; width as usize * height as usize * 3],
            written: vec![false; height as usize],
        }
    }

    /// A preview that fits the scene into `max_w` by `max_h`.
    pub fn fitted(scene_w: u32, scene_h: u32, max_w: u32, max_h: u32) -> Self {
        let (w, h) = fit(scene_w, scene_h, max_w, max_h);
        Preview::new(scene_w, scene_h, w, h)
    }

    pub fn width(&self) -> u32 {
        self.width
    }

    pub fn height(&self) -> u32 {
        self.height
    }

    pub fn scene_width(&self) -> u32 {
        self.scene_w
    }

    pub fn scene_height(&self) -> u32 {
        self.scene_h
    }

    /// The image row of scene line `line`, or None past the scene.
    pub fn row_of_line(&self, line: u32) -> Option<u32> {
        if line >= self.scene_h {
            return None;
        }
        Some((line as u64 * self.height as u64 / self.scene_h as u64) as u32)
    }

    /// Write one band line, as DNs, into one channel of its row.
    pub fn put_band_line(&mut self, line: u32, channel: Channel, dn: &[u16]) {
        self.put_with(line, channel, dn.len(), |i| dn[i]);
    }

    /// Write one band line, as the little-endian pixel bytes of a LINE
    /// DATA payload, into one channel of its row.
    pub fn put_band_line_le(&mut self, line: u32, channel: Channel, bytes: &[u8]) {
        self.put_with(line, channel, bytes.len() / 2, |i| {
            bytes[2 * i] as u16 | (bytes[2 * i + 1] as u16) << 8
        });
    }

    fn put_with(&mut self, line: u32, channel: Channel, n: usize, dn: impl Fn(usize) -> u16) {
        let Some(y) = self.row_of_line(line) else {
            return;
        };
        for x in 0..self.width {
            let src = (x as u64 * self.scene_w as u64 / self.width as u64) as usize;
            if src < n {
                let i = (y as usize * self.width as usize + x as usize) * 3;
                self.rgb[i + channel.offset()] = dn_to_u8(dn(src));
            }
        }
        self.written[y as usize] = true;
    }

    /// The pixel at column `x` of row `y`, as red, green, and blue.
    pub fn pixel(&self, x: u32, y: u32) -> [u8; 3] {
        let i = (y as usize * self.width as usize + x as usize) * 3;
        [self.rgb[i], self.rgb[i + 1], self.rgb[i + 2]]
    }

    /// The image as packed RGB rows, top row first.
    pub fn rgb(&self) -> &[u8] {
        &self.rgb
    }

    /// The rows that a line has written.
    pub fn rows_written(&self) -> u32 {
        self.written.iter().filter(|&&w| w).count() as u32
    }
}

#[cfg(test)]
mod tests;
