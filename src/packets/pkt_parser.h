#ifndef XSCAPE_PKT_PARSER_H
#define XSCAPE_PKT_PARSER_H

#include <stdint.h>
#include <stddef.h>

/* Parser error codes. */
#define XS_ERR_BAD_SYNC    -1
#define XS_ERR_BAD_FLAGS   -2
#define XS_ERR_TRUNCATED   -3
#define XS_ERR_BAD_CRC     -4

/* Parsed header info. */
typedef struct {
    uint8_t  id;
    uint32_t payload_len;
    size_t   total_len;     /* header + payload + footer (if CRC enabled) */
    int      has_crc;
    int      is_encrypted;
} xs_parsed_header_t;

/*
 * Find the next sync word (0x5353) in a byte stream.
 * Returns the byte offset, or -1 if not found.
 */
int xs_find_sync(const uint8_t *data, size_t len);

/*
 * Parse a packet header from a buffer.
 * On success, fills parsed and returns 0.
 * On error, returns XS_ERR_*.
 */
int xs_parse_header(const uint8_t *buf, size_t buf_len,
                    xs_parsed_header_t *parsed);

/*
 * Validate the CRC-32 footer of a complete packet.
 * buf must contain the full packet (header + payload + footer).
 * Returns 0 on success, XS_ERR_BAD_CRC on mismatch.
 */
int xs_validate_crc(const uint8_t *buf, size_t total_len);

#endif /* XSCAPE_PKT_PARSER_H */
