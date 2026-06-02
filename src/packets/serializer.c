#include "serializer.h"
#include "packets.h"
#include "crc32.h"
#include <string.h>

static size_t finalize_packet(uint8_t *buf, uint8_t id, size_t payload_len)
{
    xs_header_t *hdr = (xs_header_t *)buf;
    hdr->sync = XS_SYNC_WORD;
    hdr->flags = xs_make_flags(1, 0);
    hdr->id = id;
    xs_encode_u24(hdr->length, (uint32_t)payload_len);
    hdr->subpacket = 0;

    size_t total = XS_HEADER_SIZE + payload_len;
    uint32_t crc = xs_crc32(buf, total);
    memcpy(buf + total, &crc, XS_FOOTER_SIZE);

    return total + XS_FOOTER_SIZE;
}

size_t xs_serialize_session_start(uint8_t *buf, size_t buf_size,
                                   uint8_t fmt_major, uint8_t fmt_minor,
                                   uint16_t platform_id, uint16_t instrument_id,
                                   uint32_t session_id)
{
    size_t needed = XS_HEADER_SIZE + 12 + XS_FOOTER_SIZE;
    if (buf_size < needed) return 0;

    memset(buf, 0, needed);
    xs_session_start_t *pl = (xs_session_start_t *)(buf + XS_HEADER_SIZE);
    pl->data_format_major = fmt_major;
    pl->data_format_minor = fmt_minor;
    pl->platform_id = platform_id;
    pl->instrument_id = instrument_id;
    pl->session_id = session_id;

    return finalize_packet(buf, XS_PKT_SESSION_START, 12);
}

size_t xs_serialize_session_end(uint8_t *buf, size_t buf_size,
                                 uint32_t session_id)
{
    size_t needed = XS_HEADER_SIZE + 4 + XS_FOOTER_SIZE;
    if (buf_size < needed) return 0;

    memset(buf, 0, needed);
    xs_session_end_t *pl = (xs_session_end_t *)(buf + XS_HEADER_SIZE);
    pl->session_id = session_id;

    return finalize_packet(buf, XS_PKT_SESSION_END, 4);
}

size_t xs_serialize_scene_start(uint8_t *buf, size_t buf_size,
                                 uint16_t scene_number, uint8_t scene_type,
                                 uint32_t scene_height, uint16_t scene_width)
{
    size_t needed = XS_HEADER_SIZE + 12 + XS_FOOTER_SIZE;
    if (buf_size < needed) return 0;

    memset(buf, 0, needed);
    xs_scene_start_t *pl = (xs_scene_start_t *)(buf + XS_HEADER_SIZE);
    pl->scene_number = scene_number;
    pl->scene_type = scene_type;
    xs_encode_u24(pl->scene_height, scene_height);
    pl->scene_width = scene_width;

    return finalize_packet(buf, XS_PKT_SCENE_START, 12);
}

size_t xs_serialize_exposure_start(uint8_t *buf, size_t buf_size,
                                    uint64_t imager_time)
{
    size_t needed = XS_HEADER_SIZE + 8 + XS_FOOTER_SIZE;
    if (buf_size < needed) return 0;

    memset(buf, 0, needed);
    xs_exposure_start_t *pl = (xs_exposure_start_t *)(buf + XS_HEADER_SIZE);
    pl->imager_time = imager_time;

    return finalize_packet(buf, XS_PKT_EXPOSURE_START, 8);
}

size_t xs_serialize_line_data(uint8_t *buf, size_t buf_size,
                               uint8_t spectral_band, uint32_t line_number,
                               uint16_t line_length, uint8_t encoding,
                               const void *pixel_data, size_t pixel_bytes)
{
    size_t payload_len = sizeof(xs_line_data_hdr_t) + pixel_bytes;
    /* pad to 4-byte boundary */
    size_t padded = (payload_len + 3) & ~(size_t)3;
    size_t needed = XS_HEADER_SIZE + padded + XS_FOOTER_SIZE;
    if (buf_size < needed) return 0;

    memset(buf, 0, needed);
    xs_line_data_hdr_t *lh = (xs_line_data_hdr_t *)(buf + XS_HEADER_SIZE);
    lh->spectral_band = spectral_band;
    xs_encode_u24(lh->line_number, line_number);
    lh->line_length = line_length;
    lh->format = XS_FORMAT_SINGLE_BAND;
    lh->encoding = encoding;

    memcpy(buf + XS_HEADER_SIZE + sizeof(xs_line_data_hdr_t),
           pixel_data, pixel_bytes);

    return finalize_packet(buf, XS_PKT_LINE_DATA, padded);
}

size_t xs_serialize_imager_telemetry(uint8_t *buf, size_t buf_size,
                                      uint64_t imager_time,
                                      int8_t sensor_temperature)
{
    size_t needed = XS_HEADER_SIZE + 12 + XS_FOOTER_SIZE;
    if (buf_size < needed) return 0;

    memset(buf, 0, needed);
    xs_imager_telemetry_t *pl = (xs_imager_telemetry_t *)(buf + XS_HEADER_SIZE);
    pl->imager_time = imager_time;
    pl->sensor_temperature = sensor_temperature;

    return finalize_packet(buf, XS_PKT_IMAGER_TELEMETRY, 12);
}

size_t xs_serialize_imager_info(uint8_t *buf, size_t buf_size,
                                uint32_t product_id, uint16_t serial,
                                uint8_t fw_major, uint8_t fw_minor,
                                uint8_t sw_major, uint8_t sw_minor,
                                uint16_t baseline)
{
    size_t needed = XS_HEADER_SIZE + 12 + XS_FOOTER_SIZE;
    if (buf_size < needed) return 0;

    memset(buf, 0, needed);
    xs_imager_info_t *pl = (xs_imager_info_t *)(buf + XS_HEADER_SIZE);
    xs_encode_u24(pl->imager_product_id, product_id);
    pl->imager_serial = serial;
    pl->fw_major = fw_major;
    pl->fw_minor = fw_minor;
    pl->sw_major = sw_major;
    pl->sw_minor = sw_minor;
    pl->baseline_number = baseline;

    return finalize_packet(buf, XS_PKT_IMAGER_INFO, 12);
}

size_t xs_serialize_imager_config(uint8_t *buf, size_t buf_size,
                                   uint32_t line_period, uint8_t n_bands,
                                   uint32_t exposure_time,
                                   const uint8_t *band_setup,
                                   uint8_t binning_factor,
                                   uint8_t thumbnail_factor,
                                   const uint16_t *band_start_rows,
                                   const uint16_t *band_wavelengths,
                                   uint8_t scan_direction)
{
    /* Payload: fixed(12) + band_setup(n) + binning(1) + thumbnail(1)
     *        + band_start_rows(n*2) + band_wavelengths(n*2) + scan_dir(1)
     *        + padding to 4-byte boundary */
    size_t payload_len = 12 + n_bands + 2 + n_bands * 2 + n_bands * 2 + 1;
    size_t padded = (payload_len + 3) & ~(size_t)3;
    size_t needed = XS_HEADER_SIZE + padded + XS_FOOTER_SIZE;
    if (buf_size < needed) return 0;

    memset(buf, 0, needed);
    uint8_t *p = buf + XS_HEADER_SIZE;

    /* Fixed part */
    xs_imager_config_fixed_t *fixed = (xs_imager_config_fixed_t *)p;
    fixed->line_period = line_period;
    fixed->spectral_bands = n_bands;
    fixed->exposure_time = exposure_time;
    p += sizeof(xs_imager_config_fixed_t);

    /* Band Setup array */
    memcpy(p, band_setup, n_bands);
    p += n_bands;

    /* Binning and Thumbnail factors */
    *p++ = binning_factor;
    *p++ = thumbnail_factor;

    /* Band Start Rows */
    memcpy(p, band_start_rows, n_bands * 2);
    p += n_bands * 2;

    /* Band Centre Wavelengths */
    memcpy(p, band_wavelengths, n_bands * 2);
    p += n_bands * 2;

    /* Scan Direction */
    *p = scan_direction;

    return finalize_packet(buf, XS_PKT_IMAGER_CONFIG, padded);
}

size_t xs_serialize_time_sync(uint8_t *buf, size_t buf_size,
                               uint64_t imager_time, uint8_t time_format,
                               uint64_t platform_time)
{
    size_t needed = XS_HEADER_SIZE + 20 + XS_FOOTER_SIZE;
    if (buf_size < needed) return 0;

    memset(buf, 0, needed);
    xs_time_sync_t *pl = (xs_time_sync_t *)(buf + XS_HEADER_SIZE);
    pl->imager_time = imager_time;
    pl->time_format = time_format;
    pl->platform_time = platform_time;

    return finalize_packet(buf, XS_PKT_TIME_SYNC, 20);
}

size_t xs_serialize_thumbnail_line_data(uint8_t *buf, size_t buf_size,
                                         uint8_t spectral_band,
                                         uint32_t line_number,
                                         uint16_t line_length,
                                         const void *pixel_data,
                                         size_t pixel_bytes)
{
    /* Thumbnails are always 8-bit per ICD 4.3.6 */
    size_t payload_len = sizeof(xs_line_data_hdr_t) + pixel_bytes;
    size_t padded = (payload_len + 3) & ~(size_t)3;
    size_t needed = XS_HEADER_SIZE + padded + XS_FOOTER_SIZE;
    if (buf_size < needed) return 0;

    memset(buf, 0, needed);
    xs_line_data_hdr_t *lh = (xs_line_data_hdr_t *)(buf + XS_HEADER_SIZE);
    lh->spectral_band = spectral_band;
    xs_encode_u24(lh->line_number, line_number);
    lh->line_length = line_length;
    lh->format = XS_FORMAT_SINGLE_BAND;
    lh->encoding = XS_ENC_8BIT;

    memcpy(buf + XS_HEADER_SIZE + sizeof(xs_line_data_hdr_t),
           pixel_data, pixel_bytes);

    return finalize_packet(buf, XS_PKT_THUMBNAIL_DATA, padded);
}
