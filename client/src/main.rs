use std::io::{self, Read};
use std::net::TcpStream;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use clap::Parser;
use crossterm::{
    event::{self, Event, KeyCode, KeyEventKind},
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
    ExecutableCommand,
};
use image::{DynamicImage, ImageBuffer, Rgb};
use ratatui::{
    prelude::*,
    widgets::{Block, Borders, Gauge, Paragraph},
};
use ratatui_image::{picker::Picker, protocol::StatefulProtocol, StatefulImage};

#[derive(Parser)]
#[command(name = "hs100-client", about = "HyperScape100 emulator TUI client")]
struct Args {
    #[arg(long, default_value = "localhost")]
    host: String,
    #[arg(long, default_value_t = 4001)]
    data_port: u16,
    #[arg(long, default_value_t = 4002)]
    control_port: u16,
}

#[derive(Default, Clone)]
struct SessionStats {
    exposure_start: u32,
    line_data: u32,
    imager_telemetry: u32,
    crc_errors: u32,
    total_packets: u32,
    bytes_received: u64,
    stream_duration_ms: u64,
    n_bands: u8,
    band_wavelengths: [u16; 32],
    width: u16,
    scene_height: u32,
    current_line: u32,
    status: String,
}

enum AppMode {
    Normal,
    BandSelect,
}

struct App {
    stats: Arc<Mutex<SessionStats>>,
    scene_image: Arc<Mutex<Option<ImageBuffer<Rgb<u8>, Vec<u8>>>>>,
    should_quit: bool,
    picker: Picker,
    image_state: Option<StatefulProtocol>,
    last_image_update: Instant,
    mode: AppMode,
    band_enabled: [bool; 32],
    band_cursor: usize,
    band_wavelengths: [u16; 32],
    total_bands: u8,
    zoom: f64,       // 1.0 = fit-to-view, 2.0 = 2x zoom, etc.
    pan_x: i32,      // pixel offset from center (in preview image coords)
    pan_y: i32,
    rotation: u16,   // 0-350 in 10° steps
    view_w: u32,     // dimensions of the last rendered view (after crop/rotate)
    view_h: u32,
}

fn receiver_thread(
    addr: String,
    stats: Arc<Mutex<SessionStats>>,
    scene_image: Arc<Mutex<Option<ImageBuffer<Rgb<u8>, Vec<u8>>>>>,
) {
    {
        let mut s = stats.lock().unwrap();
        s.status = "Connecting...".to_string();
    }

    let mut stream = match TcpStream::connect(&addr) {
        Ok(s) => s,
        Err(e) => {
            let mut s = stats.lock().unwrap();
            s.status = format!("Connection failed: {}", e);
            return;
        }
    };

    stream.set_read_timeout(Some(Duration::from_secs(5))).ok();

    {
        let mut s = stats.lock().unwrap();
        s.status = "Connected".to_string();
    }

    let mut buf = vec![0u8; 64 * 1024];
    let mut accum = Vec::with_capacity(1024 * 1024);
    let stream_start = Instant::now();

    loop {
        match stream.read(&mut buf) {
            Ok(0) => {
                let mut s = stats.lock().unwrap();
                s.stream_duration_ms = stream_start.elapsed().as_millis() as u64;
                s.status = "Streaming complete".to_string();
                break;
            }
            Ok(n) => {
                {
                    let mut s = stats.lock().unwrap();
                    s.bytes_received += n as u64;
                    s.stream_duration_ms = stream_start.elapsed().as_millis() as u64;
                    s.status = "Streaming".to_string();
                }
                accum.extend_from_slice(&buf[..n]);
                process_packets(&mut accum, &stats, &scene_image);
            }
            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => continue,
            Err(e) => {
                let mut s = stats.lock().unwrap();
                s.stream_duration_ms = stream_start.elapsed().as_millis() as u64;
                s.status = format!("Error: {}", e);
                break;
            }
        }
    }
}

fn process_packets(
    data: &mut Vec<u8>,
    stats: &Arc<Mutex<SessionStats>>,
    scene_image: &Arc<Mutex<Option<ImageBuffer<Rgb<u8>, Vec<u8>>>>>,
) {
    let mut pos = 0;

    while pos + 8 <= data.len() {
        if data[pos] != 0x53 || data[pos + 1] != 0x53 {
            pos += 1;
            continue;
        }

        let payload_len = data[pos + 4] as usize
            | (data[pos + 5] as usize) << 8
            | (data[pos + 6] as usize) << 16;

        let flags = data[pos + 2];
        let has_crc = (flags & 0x01) != 0;
        let total_len = 8 + payload_len + if has_crc { 4 } else { 0 };

        if pos + total_len > data.len() {
            break;
        }

        let id = data[pos + 3];
        let payload = &data[pos + 8..pos + 8 + payload_len];

        {
            let mut s = stats.lock().unwrap();
            s.total_packets += 1;

            match id {
                0x00 => {} // Session Start
                0x01 => {
                    s.status = "Streaming complete".to_string();
                }
                0x02 => {
                    if payload.len() >= 12 {
                        let height = payload[4] as u32
                            | (payload[5] as u32) << 8
                            | (payload[6] as u32) << 16;
                        let width = payload[8] as u16 | (payload[9] as u16) << 8;
                        s.scene_height = height;
                        s.width = width;

                        drop(s);
                        // Scale preview to fit, preserving aspect ratio
                        let max_w: u32 = 800;
                        let max_h: u32 = 1200;
                        let aspect = width as f64 / height.max(1) as f64;
                        let (preview_w, preview_h) = if aspect > max_w as f64 / max_h as f64 {
                            (max_w, (max_w as f64 / aspect) as u32)
                        } else {
                            ((max_h as f64 * aspect) as u32, max_h)
                        };
                        let mut img = scene_image.lock().unwrap();
                        *img = Some(ImageBuffer::new(preview_w.max(1), preview_h.max(1)));
                        pos += total_len;
                        continue;
                    }
                }
                0x03 => s.exposure_start += 1,
                0x04 => {
                    s.line_data += 1;
                    if payload.len() >= 12 {
                        let band = payload[0];
                        let line_num = payload[4] as u32
                            | (payload[5] as u32) << 8
                            | (payload[6] as u32) << 16;

                        if band == 0 {
                            s.current_line = line_num + 1;
                        }

                        let width = s.width;
                        let n_bands = s.n_bands;
                        let scene_height = s.scene_height;
                        drop(s);

                        if n_bands > 0
                            && (band == 0 || band == n_bands / 2 || band == n_bands - 1)
                        {
                            let pixel_start = 12;
                            let pixel_end = payload_len.min(payload.len());
                            if pixel_start < pixel_end {
                                update_image(
                                    scene_image,
                                    &payload[pixel_start..pixel_end],
                                    line_num,
                                    scene_height,
                                    width,
                                    band,
                                    n_bands,
                                );
                            }
                        }

                        pos += total_len;
                        continue;
                    }
                }
                0x07 => {} // Time Sync
                0xA0 => {} // Imager Info
                0xA1 => {
                    if payload.len() >= 12 {
                        let n = payload[4] as usize;
                        s.n_bands = n as u8;
                        // Extract wavelengths: fixed(12) + band_setup(n) + binning(1) + thumbnail(1) + band_start_rows(n*2) + wavelengths(n*2)
                        let wl_offset = 12 + n + 2 + n * 2;
                        if payload.len() >= wl_offset + n * 2 {
                            for i in 0..n.min(32) {
                                let off = wl_offset + i * 2;
                                s.band_wavelengths[i] = payload[off] as u16
                                    | (payload[off + 1] as u16) << 8;
                            }
                        }
                    }
                }
                0xA3 => {
                    s.imager_telemetry += 1;
                }
                _ => {}
            }
        }

        pos += total_len;
    }

    data.drain(..pos);
}

fn update_image(
    scene_image: &Arc<Mutex<Option<ImageBuffer<Rgb<u8>, Vec<u8>>>>>,
    pixel_data: &[u8],
    line_num: u32,
    scene_height: u32,
    width: u16,
    band: u8,
    n_bands: u8,
) {
    let mut img_lock = scene_image.lock().unwrap();
    if let Some(ref mut img) = *img_lock {
        let img_w = img.width();
        let img_h = img.height();

        let y = if scene_height > 0 {
            (line_num as u64 * img_h as u64 / scene_height as u64) as u32
        } else {
            line_num
        };
        if y >= img_h {
            return;
        }

        let channel: usize = if band == 0 {
            2 // blue
        } else if band == n_bands / 2 {
            1 // green
        } else {
            0 // red
        };

        let src_pixels = pixel_data.len() / 2;
        for x in 0..img_w {
            let src_x = (x as u64 * width as u64 / img_w as u64) as usize;
            if src_x < src_pixels && src_x * 2 + 1 < pixel_data.len() {
                let dn = pixel_data[src_x * 2] as u16
                    | (pixel_data[src_x * 2 + 1] as u16) << 8;
                let val = ((dn.min(4095) as f32 / 4095.0) * 255.0) as u8;
                let pixel = img.get_pixel_mut(x, y);
                pixel.0[channel] = val;
            }
        }
    }
}

fn draw_ui(frame: &mut Frame, app: &mut App) {
    let stats = app.stats.lock().unwrap().clone();
    let is_streaming = stats.status == "Streaming";
    let rate = if is_streaming && stats.stream_duration_ms > 0 {
        stats.bytes_received as f64 / (stats.stream_duration_ms as f64 / 1000.0) / 1_000_000.0
    } else {
        0.0
    };

    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3),
            Constraint::Length(3),
            Constraint::Min(14),
            Constraint::Length(3),
        ])
        .split(frame.area());

    // Header
    let header = Paragraph::new(format!(
        " Status: {}",
        stats.status,
    ))
    .block(Block::default().borders(Borders::ALL).title(" HyperScape100 Client "));
    frame.render_widget(header, chunks[0]);

    // Progress
    let progress = if stats.scene_height > 0 {
        stats.current_line as f64 / stats.scene_height as f64
    } else {
        0.0
    };
    let gauge = Gauge::default()
        .block(Block::default().borders(Borders::ALL).title(" Capture "))
        .gauge_style(Style::default().fg(Color::Green))
        .ratio(progress.clamp(0.0, 1.0))
        .label(format!(
            "{}/{} lines  ({:.0}%)",
            stats.current_line,
            stats.scene_height,
            progress * 100.0
        ));
    frame.render_widget(gauge, chunks[1]);

    // Stats + scene preview
    let mid = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([Constraint::Percentage(45), Constraint::Percentage(55)])
        .split(chunks[2]);

    let wl_range = if stats.n_bands > 0 && stats.band_wavelengths[0] > 0 {
        let last = stats.band_wavelengths[stats.n_bands.saturating_sub(1) as usize];
        format!("{}-{} nm", stats.band_wavelengths[0], last)
    } else {
        "—".to_string()
    };

    let stats_text = format!(
        "\n\
         Bands:      {} ({})\n\
         Width:      {} px\n\
         Lines:      {}\n\
         \n\
         Packets received:\n\
         EXPOSURE    {:>8}\n\
         LINE_DATA   {:>8}\n\
         TELEMETRY   {:>8}\n\
         CRC errors  {:>8}\n\
         Total       {:>8}\n\
         \n\
         Rate: {:.1} MB/s\n\
         Received: {:.1} MB",
        stats.n_bands,
        wl_range,
        stats.width,
        stats.scene_height,
        stats.exposure_start,
        stats.line_data,
        stats.imager_telemetry,
        stats.crc_errors,
        stats.total_packets,
        rate,
        stats.bytes_received as f64 / 1_000_000.0,
    );

    let stats_widget =
        Paragraph::new(stats_text).block(Block::default().borders(Borders::ALL).title(" Packets "));
    frame.render_widget(stats_widget, mid[0]);

    // Scene preview
    let mut view_label = String::new();
    if app.zoom > 1.01 {
        view_label += &format!(" {:.0}x", app.zoom);
    }
    if app.rotation > 0 {
        view_label += &format!(" {}°", app.rotation);
    }
    let scene_block = Block::default().borders(Borders::ALL).title(format!(
        " Scene (R:{} G:{} B:0){} ",
        stats.n_bands.saturating_sub(1),
        stats.n_bands / 2,
        view_label,
    ));

    if let Some(ref mut img_state) = app.image_state {
        let inner = scene_block.inner(mid[1]);
        frame.render_widget(scene_block, mid[1]);

        // Center horizontally using the Picker's font size and actual view dimensions
        let font_size = app.picker.font_size();
        let vw = app.view_w.max(1);
        let vh = app.view_h.max(1);

        // How many cells the image occupies when fit to inner height
        let img_px_per_cell_h = font_size.1.max(1) as u32;
        let rendered_h_px = inner.height as u32 * img_px_per_cell_h;
        let scale = rendered_h_px as f64 / vh as f64;
        let rendered_w_px = (vw as f64 * scale) as u32;
        let img_w_cells = (rendered_w_px / font_size.0.max(1) as u32) as u16;

        let render_area = if img_w_cells < inner.width {
            let pad = (inner.width - img_w_cells) / 2;
            Rect::new(inner.x + pad, inner.y, img_w_cells.max(1), inner.height)
        } else {
            inner
        };

        let image_widget = StatefulImage::default();
        frame.render_stateful_widget(image_widget, render_area, img_state);
    } else {
        let preview = Paragraph::new("\n Waiting for scene data...")
            .block(scene_block);
        frame.render_widget(preview, mid[1]);
    }

    // Footer
    let footer_text = match app.mode {
        AppMode::Normal => " [B] Bands  [P/N] Scene  [+/-] Zoom  [Arrows] Pan  [</>] Rotate  [0] Reset  [Q] Quit ",
        AppMode::BandSelect => " [↑↓] Navigate  [Space] Toggle  [A] All  [Enter] Apply  [Esc] Cancel ",
    };
    let footer = Paragraph::new(footer_text)
        .block(Block::default().borders(Borders::ALL));
    frame.render_widget(footer, chunks[3]);

    // Band selector overlay
    if let AppMode::BandSelect = app.mode {
        let area = centered_rect(35, 80, frame.area());
        let inner_h = area.height.saturating_sub(2) as usize; // minus borders

        // Build band list
        let mut lines: Vec<Line> = Vec::new();
        let enabled_count = (0..app.total_bands as usize)
            .filter(|i| app.band_enabled[*i])
            .count();
        lines.push(Line::from(format!(" {} of {} bands enabled", enabled_count, app.total_bands)));
        lines.push(Line::from(""));

        for i in 0..app.total_bands as usize {
            let marker = if app.band_enabled[i] { "[x]" } else { "[ ]" };
            let wl = if app.band_wavelengths[i] > 0 {
                format!("{:>3} nm", app.band_wavelengths[i])
            } else {
                format!("band {:>2}", i)
            };
            let cursor = if i == app.band_cursor { ">" } else { " " };
            let style = if i == app.band_cursor {
                Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD)
            } else if app.band_enabled[i] {
                Style::default().fg(Color::Green)
            } else {
                Style::default().fg(Color::DarkGray)
            };
            lines.push(Line::from(Span::styled(
                format!(" {} {} {}", cursor, marker, wl), style
            )));
        }

        // Scroll so cursor is visible
        let header_lines = 2;
        let scroll = if app.band_cursor + header_lines >= inner_h {
            (app.band_cursor + header_lines - inner_h + 1) as u16
        } else {
            0
        };

        let selector = Paragraph::new(lines)
            .block(Block::default().borders(Borders::ALL).title(" Bands "))
            .scroll((scroll, 0));

        frame.render_widget(ratatui::widgets::Clear, area);
        frame.render_widget(selector, area);
    }
}

fn centered_rect(percent_x: u16, percent_y: u16, r: Rect) -> Rect {
    let popup_layout = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Percentage((100 - percent_y) / 2),
            Constraint::Percentage(percent_y),
            Constraint::Percentage((100 - percent_y) / 2),
        ])
        .split(r);
    Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Percentage((100 - percent_x) / 2),
            Constraint::Percentage(percent_x),
            Constraint::Percentage((100 - percent_x) / 2),
        ])
        .split(popup_layout[1])[1]
}

fn main() -> io::Result<()> {
    let args = Args::parse();

    let stats = Arc::new(Mutex::new(SessionStats::default()));
    let scene_image: Arc<Mutex<Option<ImageBuffer<Rgb<u8>, Vec<u8>>>>> =
        Arc::new(Mutex::new(None));

    let addr = format!("{}:{}", args.host, args.data_port);
    let stats_clone = Arc::clone(&stats);
    let image_clone = Arc::clone(&scene_image);

    let control_addr = format!("{}:{}", args.host, args.control_port);

    // Shared flag to signal receiver thread to reconnect
    let reconnect = Arc::new(Mutex::new(false));
    let reconnect_clone = Arc::clone(&reconnect);

    thread::spawn(move || {
        receiver_thread(addr.clone(), Arc::clone(&stats_clone), Arc::clone(&image_clone));

        // After first session, wait for reconnect signals
        loop {
            thread::sleep(Duration::from_millis(100));
            let mut r = reconnect_clone.lock().unwrap();
            if *r {
                *r = false;
                drop(r);

                // Reset stats
                let mut s = stats_clone.lock().unwrap();
                *s = SessionStats::default();
                s.status = "Reconnecting...".to_string();
                drop(s);

                // Clear image
                let mut img = image_clone.lock().unwrap();
                *img = None;
                drop(img);

                // Connect and receive next session
                receiver_thread(addr.clone(), Arc::clone(&stats_clone), Arc::clone(&image_clone));
            }
        }
    });

    enable_raw_mode()?;
    io::stdout().execute(EnterAlternateScreen)?;
    let mut terminal = Terminal::new(CrosstermBackend::new(io::stdout()))?;

    let picker = Picker::from_query_stdio().unwrap_or_else(|_| {
        Picker::from_fontsize((8, 16))
    });

    let mut app = App {
        stats,
        scene_image,
        should_quit: false,
        picker,
        image_state: None,
        last_image_update: Instant::now(),
        mode: AppMode::Normal,
        band_enabled: [true; 32],
        band_cursor: 0,
        band_wavelengths: [0u16; 32],
        total_bands: 0,
        zoom: 1.0,
        pan_x: 0,
        pan_y: 0,
        rotation: 0,
        view_w: 0,
        view_h: 0,
    };

    while !app.should_quit {
        // Sync band info from stats -- only when wavelengths are available
        {
            let s = app.stats.lock().unwrap();
            if s.n_bands > 0 && s.band_wavelengths[0] > 0 && app.total_bands == 0 {
                app.total_bands = s.n_bands;
                app.band_wavelengths = s.band_wavelengths;
                for i in 0..s.n_bands as usize {
                    app.band_enabled[i] = true;
                }
            }
        }

        if app.last_image_update.elapsed() > Duration::from_millis(200) {
            let img_lock = app.scene_image.lock().unwrap();
            if let Some(ref img) = *img_lock {
                let iw = img.width();
                let ih = img.height();
                let view = if app.zoom > 1.01 {
                    let crop_w = ((iw as f64 / app.zoom) as u32).max(1).min(iw);
                    let crop_h = ((ih as f64 / app.zoom) as u32).max(1).min(ih);

                    let default_x = (iw - crop_w) / 2;
                    let default_y = (ih - crop_h) / 2;

                    let x = (default_x as i32 + app.pan_x)
                        .max(0).min((iw - crop_w) as i32) as u32;
                    let y = (default_y as i32 + app.pan_y)
                        .max(0).min((ih - crop_h) as i32) as u32;

                    let cropped = image::imageops::crop_imm(img, x, y, crop_w, crop_h)
                        .to_image();

                    // Scale cropped region back up to full preview size
                    let scaled = image::imageops::resize(
                        &cropped, iw, ih, image::imageops::FilterType::Nearest
                    );
                    DynamicImage::ImageRgb8(scaled)
                } else {
                    DynamicImage::ImageRgb8(img.clone())
                };
                // Apply rotation
                let rotated = match app.rotation {
                    0 => view,
                    90 => view.rotate90(),
                    180 => view.rotate180(),
                    270 => view.rotate270(),
                    deg => {
                        // Arbitrary-angle spatial rotation via inverse mapping
                        let (w, h) = (view.width(), view.height());
                        let angle = deg as f64 * std::f64::consts::PI / 180.0;
                        let cos_a = angle.cos();
                        let sin_a = angle.sin();
                        let new_w = ((w as f64 * cos_a.abs()) + (h as f64 * sin_a.abs())) as u32;
                        let new_h = ((w as f64 * sin_a.abs()) + (h as f64 * cos_a.abs())) as u32;
                        let cx = w as f64 / 2.0;
                        let cy = h as f64 / 2.0;
                        let ncx = new_w as f64 / 2.0;
                        let ncy = new_h as f64 / 2.0;
                        let mut out = ImageBuffer::new(new_w, new_h);
                        let rgb = view.to_rgb8();
                        for ny in 0..new_h {
                            for nx in 0..new_w {
                                let dx = nx as f64 - ncx;
                                let dy = ny as f64 - ncy;
                                let sx = (dx * cos_a + dy * sin_a + cx) as i32;
                                let sy = (-dx * sin_a + dy * cos_a + cy) as i32;
                                if sx >= 0 && sx < w as i32 && sy >= 0 && sy < h as i32 {
                                    out.put_pixel(nx, ny, *rgb.get_pixel(sx as u32, sy as u32));
                                }
                            }
                        }
                        DynamicImage::ImageRgb8(out)
                    }
                };
                app.view_w = rotated.width();
                app.view_h = rotated.height();
                app.image_state = Some(app.picker.new_resize_protocol(rotated));
            }
            drop(img_lock);
            app.last_image_update = Instant::now();
        }

        terminal.draw(|frame| draw_ui(frame, &mut app))?;

        if event::poll(Duration::from_millis(50))? {
            if let Event::Key(key) = event::read()? {
                if key.kind == KeyEventKind::Press {
                    match app.mode {
                        AppMode::Normal => {
                            let is_streaming = {
                                let s = app.stats.lock().unwrap();
                                s.status == "Streaming" || s.status == "Connected" || s.status == "Connecting..."
                            };
                            match key.code {
                            KeyCode::Char('q') | KeyCode::Char('Q') => app.should_quit = true,
                            KeyCode::Char('b') | KeyCode::Char('B') => {
                                if app.total_bands > 0 && !is_streaming {
                                    app.mode = AppMode::BandSelect;
                                    app.band_cursor = 0;
                                }
                            }
                            KeyCode::Char('n') | KeyCode::Char('N') if !is_streaming => {
                                send_next_scene(&control_addr, 1);
                                reset_view(&mut app);
                                *reconnect.lock().unwrap() = true;
                            }
                            KeyCode::Char('p') | KeyCode::Char('P') if !is_streaming => {
                                send_next_scene(&control_addr, -1);
                                reset_view(&mut app);
                                *reconnect.lock().unwrap() = true;
                            }
                            // Zoom
                            KeyCode::Char('+') | KeyCode::Char('=') => {
                                app.zoom = (app.zoom * 1.5).min(16.0);
                                app.image_state = None;
                                app.last_image_update = Instant::now() - Duration::from_secs(1);
                            }
                            KeyCode::Char('-') => {
                                app.zoom = (app.zoom / 1.5).max(1.0);
                                if app.zoom <= 1.01 {
                                    app.pan_x = 0;
                                    app.pan_y = 0;
                                }
                                app.image_state = None;
                                app.last_image_update = Instant::now() - Duration::from_secs(1);
                            }
                            KeyCode::Char('0') => {
                                app.zoom = 1.0;
                                app.pan_x = 0;
                                app.pan_y = 0;
                                app.rotation = 0;
                                app.last_image_update = Instant::now() - Duration::from_secs(1);
                            }
                            // Rotate
                            KeyCode::Char('>') | KeyCode::Char('.') => {
                                app.rotation = (app.rotation + 10) % 360;
                                app.image_state = None;
                                app.last_image_update = Instant::now() - Duration::from_secs(1);
                            }
                            KeyCode::Char('<') | KeyCode::Char(',') => {
                                app.rotation = (app.rotation + 350) % 360;
                                app.image_state = None;
                                app.last_image_update = Instant::now() - Duration::from_secs(1);
                            }
                            // Pan (step = 20 pixels in preview coords)
                            KeyCode::Up => {
                                if app.zoom > 1.0 {
                                    app.pan_y -= 20;
                                    app.last_image_update = Instant::now() - Duration::from_secs(1);
                                }
                            }
                            KeyCode::Down => {
                                if app.zoom > 1.0 {
                                    app.pan_y += 20;
                                    app.last_image_update = Instant::now() - Duration::from_secs(1);
                                }
                            }
                            KeyCode::Left => {
                                if app.zoom > 1.0 {
                                    app.pan_x -= 20;
                                    app.last_image_update = Instant::now() - Duration::from_secs(1);
                                }
                            }
                            KeyCode::Right => {
                                if app.zoom > 1.0 {
                                    app.pan_x += 20;
                                    app.last_image_update = Instant::now() - Duration::from_secs(1);
                                }
                            }
                            _ => {}
                        }},
                        AppMode::BandSelect => match key.code {
                            KeyCode::Up => {
                                if app.band_cursor > 0 {
                                    app.band_cursor -= 1;
                                }
                            }
                            KeyCode::Down => {
                                if app.band_cursor + 1 < app.total_bands as usize {
                                    app.band_cursor += 1;
                                }
                            }
                            KeyCode::Char(' ') => {
                                app.band_enabled[app.band_cursor] = !app.band_enabled[app.band_cursor];
                            }
                            KeyCode::Char('a') | KeyCode::Char('A') => {
                                let all_on = (0..app.total_bands as usize).all(|i| app.band_enabled[i]);
                                for i in 0..app.total_bands as usize {
                                    app.band_enabled[i] = !all_on;
                                }
                            }
                            KeyCode::Enter => {
                                let n = app.total_bands;
                                send_bands(&control_addr, &app.band_enabled, &app.band_wavelengths, n);
                                *reconnect.lock().unwrap() = true;
                                app.mode = AppMode::Normal;
                            }
                            KeyCode::Esc => {
                                app.mode = AppMode::Normal;
                            }
                            _ => {}
                        },
                    }
                }
            }
        }
    }

    disable_raw_mode()?;
    io::stdout().execute(LeaveAlternateScreen)?;

    Ok(())
}

fn reset_view(app: &mut App) {
    app.total_bands = 0;
    app.zoom = 1.0;
    app.pan_x = 0;
    app.pan_y = 0;
    app.rotation = 0;
    app.image_state = None;
}

fn send_bands(control_addr: &str, enabled: &[bool; 32], wavelengths: &[u16; 32], total: u8) {
    let mut buf = vec![0u8; 256];
    buf[0] = b'R';
    buf[1] = b'C';
    buf[2] = b'F';
    buf[3] = b'G';

    let mut n: u8 = 0;
    for i in 0..total as usize {
        if enabled[i] {
            let wl = if wavelengths[i] > 0 {
                wavelengths[i]
            } else {
                445 + (i as u16 * 880 / total as u16)
            };
            buf[5 + n as usize * 2] = (wl & 0xFF) as u8;
            buf[5 + n as usize * 2 + 1] = (wl >> 8) as u8;
            n += 1;
        }
    }
    if n == 0 { return; }
    buf[4] = n;
    let len = 5 + n as usize * 2;

    if let Ok(mut stream) = TcpStream::connect(control_addr) {
        stream.set_write_timeout(Some(Duration::from_secs(2))).ok();
        let _ = std::io::Write::write_all(&mut stream, &buf[..len]);
        let mut ack = [0u8; 4];
        stream.set_read_timeout(Some(Duration::from_secs(2))).ok();
        let _ = stream.read(&mut ack);
    }
}

fn send_next_scene(control_addr: &str, direction: i8) {
    let buf: [u8; 5] = [b'N', b'S', b'C', b'N', direction as u8];
    if let Ok(mut stream) = TcpStream::connect(control_addr) {
        stream.set_write_timeout(Some(Duration::from_secs(2))).ok();
        let _ = std::io::Write::write_all(&mut stream, &buf);
        let mut ack = [0u8; 4];
        stream.set_read_timeout(Some(Duration::from_secs(2))).ok();
        let _ = stream.read(&mut ack);
    }
}
