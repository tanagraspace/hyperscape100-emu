#include "pkt_parser.h"
#include "packets.h"
#include "crc32.h"
#include <string.h>

int xs_find_sync(const uint8_t *data, size_t len)
{
    if (len < 2) return -1;
    for (size_t i = 0; i <= len - 2; i++) {
        if (data[i] == 0x53 && data[i + 1] == 0x53) {
            return (int)i;
        }
    }
    return -1;
}

int xs_parse_header(const uint8_t *buf, size_t buf_len,
                    xs_parsed_header_t *parsed)
{
    if (buf_len < XS_HEADER_SIZE) return XS_ERR_TRUNCATED;

    const xs_header_t *hdr = (const xs_header_t *)buf;

    if (hdr->sync != XS_SYNC_WORD) return XS_ERR_BAD_SYNC;
    if (!xs_flags_valid(hdr->flags)) return XS_ERR_BAD_FLAGS;

    parsed->id = hdr->id;
    parsed->payload_len = xs_decode_u24(hdr->length);
    parsed->has_crc = (hdr->flags & XS_FLAG_CRC_EN) ? 1 : 0;
    parsed->is_encrypted = (hdr->flags & XS_FLAG_ENC_EN) ? 1 : 0;

    parsed->total_len = XS_HEADER_SIZE + parsed->payload_len;
    if (parsed->has_crc) parsed->total_len += XS_FOOTER_SIZE;

    return 0;
}

int xs_validate_crc(const uint8_t *buf, size_t total_len)
{
    if (total_len < XS_HEADER_SIZE + XS_FOOTER_SIZE) return XS_ERR_TRUNCATED;

    size_t data_len = total_len - XS_FOOTER_SIZE;
    uint32_t computed = xs_crc32(buf, data_len);

    uint32_t stored;
    memcpy(&stored, buf + data_len, XS_FOOTER_SIZE);

    return (computed == stored) ? 0 : XS_ERR_BAD_CRC;
}
