#ifndef XSCAPE_SERIALIZER_H
#define XSCAPE_SERIALIZER_H

#include <stdint.h>
#include <stddef.h>

/*
 * Packet serialization functions.
 * Each writes a complete packet (header + payload + CRC-32 footer) into buf.
 * Returns total bytes written, or 0 if buf is too small.
 */

size_t xs_serialize_session_start(uint8_t *buf, size_t buf_size,
                                   uint8_t fmt_major, uint8_t fmt_minor,
                                   uint16_t platform_id, uint16_t instrument_id,
                                   uint32_t session_id);

size_t xs_serialize_session_end(uint8_t *buf, size_t buf_size,
                                 uint32_t session_id);

size_t xs_serialize_scene_start(uint8_t *buf, size_t buf_size,
                                 uint16_t scene_number, uint8_t scene_type,
                                 uint32_t scene_height, uint16_t scene_width);

size_t xs_serialize_exposure_start(uint8_t *buf, size_t buf_size,
                                    uint64_t imager_time);

size_t xs_serialize_line_data(uint8_t *buf, size_t buf_size,
                               uint8_t spectral_band, uint32_t line_number,
                               uint16_t line_length, uint8_t encoding,
                               const void *pixel_data, size_t pixel_bytes);

size_t xs_serialize_imager_telemetry(uint8_t *buf, size_t buf_size,
                                      uint64_t imager_time,
                                      int8_t sensor_temperature);

size_t xs_serialize_imager_info(uint8_t *buf, size_t buf_size,
                                uint32_t product_id, uint16_t serial,
                                uint8_t fw_major, uint8_t fw_minor,
                                uint8_t sw_major, uint8_t sw_minor,
                                uint16_t baseline);

size_t xs_serialize_imager_config(uint8_t *buf, size_t buf_size,
                                   uint32_t line_period, uint8_t n_bands,
                                   uint32_t exposure_time,
                                   const uint8_t *band_setup,
                                   uint8_t binning_factor,
                                   uint8_t thumbnail_factor,
                                   const uint16_t *band_start_rows,
                                   const uint16_t *band_wavelengths,
                                   uint8_t scan_direction);

size_t xs_serialize_time_sync(uint8_t *buf, size_t buf_size,
                               uint64_t imager_time, uint8_t time_format,
                               uint64_t platform_time);

size_t xs_serialize_thumbnail_line_data(uint8_t *buf, size_t buf_size,
                                         uint8_t spectral_band,
                                         uint32_t line_number,
                                         uint16_t line_length,
                                         const void *pixel_data,
                                         size_t pixel_bytes);

#endif /* XSCAPE_SERIALIZER_H */
