/*
 * base/cksum.h - MainMemory CRC32 checksum.
 *
 * The checksum code is borrowed from WiredTiger repository:
 *
 * https://github.com/wiredtiger/wiredtiger
 *
 * It is released into the public domain by WiredTiger, Inc.
 */

#ifndef BASE_CKSUM_H
#define BASE_CKSUM_H

#include "common.h"

void mm_cksum_init(void);

uint32_t mm_cksum(const void *data, size_t size);

#endif /* BASE_CKSUM_H */
