#ifndef XSCAPE_PACKETS_H
#define XSCAPE_PACKETS_H

#include <stdint.h>
#include <stddef.h>

/*
 * xScape Control and Data Interface packet definitions.
 * Source: ICD doc 051715, rev 8, section 4.
 * All multi-byte fields are little-endian.
 */

/* Sync word marking the start of every packet. */
#define XS_SYNC_WORD        0x5353

/* Packet header size in bytes. */
#define XS_HEADER_SIZE      8

/* Packet footer (CRC-32) size in bytes. */
#define XS_FOOTER_SIZE      4

/* Sensor width in pixels. */
#define XS_SENSOR_WIDTH     4096

/* Maximum number of spectral bands. */
#define XS_MAX_BANDS        32

/* Maximum sub-packet count (6-bit field). */
#define XS_MAX_SUBPKT_COUNT 63

/* Maximum user ancillary data payload. */
#define XS_MAX_USER_DATA    1024

/* Default timing constants. */
#define XS_DEFAULT_LINE_PERIOD_US  1000
#define XS_TELEMETRY_INTERVAL_US   500000  /* 500ms per ICD 4.3.10.4 */

/*
 * Packet identifiers (ICD Table 4-2).
 */
#define XS_PKT_SESSION_START    0x00
#define XS_PKT_SESSION_END      0x01
#define XS_PKT_SCENE_START      0x02
#define XS_PKT_EXPOSURE_START   0x03
#define XS_PKT_LINE_DATA        0x04
#define XS_PKT_THUMBNAIL_DATA   0x05
#define XS_PKT_TIME_SYNC        0x07
#define XS_PKT_TIME_SYNC_PPS    0x08
#define XS_PKT_USER_ANCILLARY   0x80
#define XS_PKT_IMAGER_INFO      0xA0
#define XS_PKT_IMAGER_CONFIG    0xA1
#define XS_PKT_IMAGER_TELEMETRY 0xA3

/*
 * Flags byte (ICD section 4.2.1).
 * Lower 4 bits are flags, upper 4 are their complement.
 */
#define XS_FLAG_CRC_EN      (1 << 0)
#define XS_FLAG_ENC_EN      (1 << 1)

/*
 * Scene types (ICD section 4.3.3).
 */
#define XS_SCENE_SNAPSHOT           0
#define XS_SCENE_LINE_SCAN          1
#define XS_SCENE_SNAPSHOT_TEST      2
#define XS_SCENE_LINE_SCAN_TEST     3
#define XS_SCENE_VIDEO              4
#define XS_SCENE_LINE_SCAN_HI_ACC  5

/*
 * Line Data format field (ICD section 4.3.5).
 */
#define XS_FORMAT_SINGLE_BAND  0
#define XS_FORMAT_BAYER_RG     1
#define XS_FORMAT_BAYER_GR     2
#define XS_FORMAT_BAYER_GB     3
#define XS_FORMAT_BAYER_BG     4

/*
 * Line Data encoding field (ICD section 4.3.5).
 */
#define XS_ENC_8BIT            0
#define XS_ENC_10BIT_PACKED    1
#define XS_ENC_10BIT_16BIT     2
#define XS_ENC_12BIT_PACKED    3
#define XS_ENC_12BIT_16BIT     4
#define XS_ENC_16BIT           5

/*
 * CRC-32 parameters (ICD section 4.2.2).
 * Same as zlib/ethernet.
 *   Polynomial (truncated): 0x04C11DB7
 *   Initial value:          0xFFFFFFFF
 *   Reflect input:          Yes
 *   Reflect output:         Yes
 *   Final XOR:              0xFFFFFFFF
 *
 * ICD test vectors:
 *   Input 0x0000 0000 → CRC 0x2144 DF1C
 *   Input 0x0102 0304 → CRC 0xB63C FBCD
 */

/*
 * Packet header (8 bytes).
 *
 * Byte layout:
 *   [0-1] Sync     (uint16, always 0x5353, little-endian)
 *   [2]   Flags    (uint8)
 *   [3]   ID       (uint8)
 *   [4-6] Length   (uint24, little-endian, payload bytes, multiple of 4)
 *   [7]   Subpkt   (uint8)
 */
typedef struct __attribute__((packed)) {
    uint16_t sync;
    uint8_t  flags;
    uint8_t  id;
    uint8_t  length[3]; /* uint24 little-endian */
    uint8_t  subpacket;
} xs_header_t;

/*
 * Session Start payload (ID 0x00, 12 bytes).
 */
typedef struct __attribute__((packed)) {
    uint8_t  data_format_major;
    uint8_t  data_format_minor;
    uint16_t platform_id;
    uint16_t instrument_id;
    uint16_t reserved;
    uint32_t session_id;
} xs_session_start_t;

/*
 * Session End payload (ID 0x01, 4 bytes).
 */
typedef struct __attribute__((packed)) {
    uint32_t session_id;
} xs_session_end_t;

/*
 * Scene Start payload (ID 0x02, 12 bytes).
 */
typedef struct __attribute__((packed)) {
    uint16_t scene_number;
    uint8_t  scene_type;
    uint8_t  reserved1;
    uint8_t  scene_height[3]; /* uint24 little-endian */
    uint8_t  reserved2;
    uint16_t scene_width;
    uint16_t reserved3;
} xs_scene_start_t;

/*
 * Exposure Start payload (ID 0x03, 8 bytes).
 */
typedef struct __attribute__((packed)) {
    uint64_t imager_time;
} xs_exposure_start_t;

/*
 * Line Data header (ID 0x04, 12 bytes before pixel data).
 */
typedef struct __attribute__((packed)) {
    uint8_t  spectral_band;
    uint8_t  reserved1[3]; /* uint24 reserved */
    uint8_t  line_number[3]; /* uint24 little-endian */
    uint8_t  reserved2;
    uint16_t line_length;
    uint8_t  format;
    uint8_t  encoding;
} xs_line_data_hdr_t;

/*
 * Imager Information payload (ID 0xA0, 12 bytes).
 */
typedef struct __attribute__((packed)) {
    uint8_t  imager_product_id[3]; /* uint24 */
    uint8_t  reserved;
    uint16_t imager_serial;
    uint8_t  fw_major;
    uint8_t  fw_minor;
    uint8_t  sw_major;
    uint8_t  sw_minor;
    uint16_t baseline_number;
} xs_imager_info_t;

/*
 * Imager Configuration payload for line scan (ID 0xA1, variable length).
 * Fixed fields followed by per-band arrays.
 */
typedef struct __attribute__((packed)) {
    uint32_t line_period;
    uint8_t  spectral_bands;
    uint8_t  reserved1[3];
    uint32_t exposure_time;
} xs_imager_config_fixed_t;

/* Per-band fields follow the fixed part:
 *   uint8_t  band_setup[n]
 *   uint8_t  binning_factor
 *   uint8_t  thumbnail_factor
 *   uint16_t band_start_row[n]
 *   uint16_t band_centre_wavelength[n]
 *   uint8_t  scan_direction
 *   ... padding to 4-byte boundary
 */

/*
 * Imager Telemetry payload (ID 0xA3, 12 bytes).
 */
typedef struct __attribute__((packed)) {
    uint64_t imager_time;
    int8_t   sensor_temperature;
    uint8_t  reserved[3];
} xs_imager_telemetry_t;

/*
 * Time Synchronisation payload (ID 0x07, 20 bytes).
 */
typedef struct __attribute__((packed)) {
    uint64_t imager_time;
    uint8_t  time_format;
    uint8_t  reserved[3];
    uint64_t platform_time;
} xs_time_sync_t;

/*
 * Time Synchronisation PPS payload (ID 0x08, 8 bytes).
 */
typedef struct __attribute__((packed)) {
    uint64_t imager_time;
} xs_time_sync_pps_t;

/* ---------- Helper functions ---------- */

/* Build flags byte with complement check bits. */
static inline uint8_t xs_make_flags(int crc_en, int enc_en)
{
    uint8_t lower = 0;
    if (crc_en) lower |= XS_FLAG_CRC_EN;
    if (enc_en) lower |= XS_FLAG_ENC_EN;
    uint8_t upper = (~lower & 0x0F) << 4;
    return upper | lower;
}

/* Validate flags byte (upper 4 bits must be complement of lower 4). */
static inline int xs_flags_valid(uint8_t flags)
{
    uint8_t lower = flags & 0x0F;
    uint8_t upper = (flags >> 4) & 0x0F;
    return upper == (~lower & 0x0F);
}

/* Encode uint24 little-endian into 3-byte array. */
static inline void xs_encode_u24(uint8_t dst[3], uint32_t val)
{
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
    dst[2] = (uint8_t)((val >> 16) & 0xFF);
}

/* Decode uint24 little-endian from 3-byte array. */
static inline uint32_t xs_decode_u24(const uint8_t src[3])
{
    return (uint32_t)src[0]
         | ((uint32_t)src[1] << 8)
         | ((uint32_t)src[2] << 16);
}

/* Build sub-packet byte. */
static inline uint8_t xs_make_subpacket(int start, int end, uint8_t count)
{
    uint8_t val = count & 0x3F;
    if (start) val |= (1 << 6);
    if (end)   val |= (1 << 7);
    return val;
}

#endif /* XSCAPE_PACKETS_H */
