#ifndef XSCAPE_CRC32_H
#define XSCAPE_CRC32_H

#include <stdint.h>
#include <stddef.h>

/*
 * CRC-32 as specified in xScape ICD section 4.2.2.
 * Same algorithm as zlib, PKZip, and ethernet.
 *
 *   Polynomial (truncated): 0x04C11DB7
 *   Initial value:          0xFFFFFFFF
 *   Reflect input:          Yes
 *   Reflect output:         Yes
 *   Final XOR:              0xFFFFFFFF
 */

/* Compute CRC-32 over a buffer. */
uint32_t xs_crc32(const void *data, size_t len);

/* Incremental CRC-32: update with additional data. */
uint32_t xs_crc32_update(uint32_t crc, const void *data, size_t len);

/* Finalize an incremental CRC-32. */
static inline uint32_t xs_crc32_finalize(uint32_t crc)
{
    return crc ^ 0xFFFFFFFF;
}

#endif /* XSCAPE_CRC32_H */
