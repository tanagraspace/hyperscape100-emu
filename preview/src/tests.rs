use super::*;

#[test]
fn fit_keeps_the_aspect_of_a_wide_scene() {
    // 1000 x 500 is wider than 800 x 1200, so the width binds: 800 x 400
    assert_eq!(fit(1000, 500, 800, 1200), (800, 400));
}

#[test]
fn fit_keeps_the_aspect_of_a_tall_scene() {
    // 300 x 3000 is taller, so the height binds: 1200 * 0.1 = 120 x 1200
    assert_eq!(fit(300, 3000, 800, 1200), (120, 1200));
}

#[test]
fn fit_never_returns_a_zero_side() {
    // 1 x 100000 would floor to 0 pixels wide
    assert_eq!(fit(1, 100_000, 800, 1200), (1, 1200));
}

#[test]
fn channel_map_follows_the_first_middle_and_last_band() {
    assert_eq!(channel_for_band(0, 31), Some(Channel::Blue));
    assert_eq!(channel_for_band(15, 31), Some(Channel::Green));
    assert_eq!(channel_for_band(30, 31), Some(Channel::Red));
    assert_eq!(channel_for_band(14, 31), None);
    assert_eq!(channel_for_band(16, 31), None);
    assert_eq!(channel_for_band(31, 31), None);
}

#[test]
fn channel_map_gives_blue_precedence_for_one_band() {
    // band 0 is first, middle, and last at once
    assert_eq!(channel_for_band(0, 1), Some(Channel::Blue));
    assert_eq!(channel_for_band(0, 0), None);
}

#[test]
fn dn_scale_saturates_at_twelve_bits() {
    assert_eq!(dn_to_u8(0), 0);
    // 2048 / 4095 * 255 = 127.53, truncated
    assert_eq!(dn_to_u8(2048), 127);
    assert_eq!(dn_to_u8(4094), 254);
    assert_eq!(dn_to_u8(4095), 255);
    assert_eq!(dn_to_u8(60000), 255);
}

#[test]
fn row_of_line_scales_into_the_image() {
    // a 10-line scene in a 4-row image: line 5 maps to 5 * 4 / 10 = 2
    let p = Preview::new(6, 10, 3, 4);
    assert_eq!(p.row_of_line(0), Some(0));
    assert_eq!(p.row_of_line(5), Some(2));
    assert_eq!(p.row_of_line(9), Some(3));
    assert_eq!(p.row_of_line(10), None);
}

#[test]
fn put_band_line_samples_columns_into_one_channel() {
    // a 6-pixel line in a 3-pixel image takes source pixels 0, 2 and 4
    let mut p = Preview::new(6, 10, 3, 4);
    let dn = [4095u16, 1, 2048, 3, 0, 5];
    p.put_band_line(5, Channel::Red, &dn);
    assert_eq!(p.pixel(0, 2), [255, 0, 0]);
    assert_eq!(p.pixel(1, 2), [127, 0, 0]);
    assert_eq!(p.pixel(2, 2), [0, 0, 0]);
    p.put_band_line(5, Channel::Blue, &dn);
    assert_eq!(p.pixel(0, 2), [255, 0, 255]);
    // other rows stay black
    assert_eq!(p.pixel(0, 1), [0, 0, 0]);
    assert_eq!(p.rows_written(), 1);
}

#[test]
fn put_band_line_le_reads_little_endian_pixels() {
    // 0x0FFF little endian is ff 0f. A short line leaves the rest alone
    let mut p = Preview::new(2, 1, 2, 1);
    p.put_band_line_le(0, Channel::Green, &[0xff, 0x0f, 0x00]);
    assert_eq!(p.pixel(0, 0), [0, 255, 0]);
    assert_eq!(p.pixel(1, 0), [0, 0, 0]);
}

#[test]
fn put_band_line_ignores_a_line_past_the_scene() {
    let mut p = Preview::new(2, 4, 2, 4);
    p.put_band_line(4, Channel::Red, &[4095, 4095]);
    assert!(p.rgb().iter().all(|&v| v == 0));
    assert_eq!(p.rows_written(), 0);
}
