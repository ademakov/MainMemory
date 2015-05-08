/*
 * memcache/binary.h - MainMemory memcache binary protocol support.
 *
 * Copyright (C) 2015  Aleksey Demakov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MEMCACHE_BINARY_H
#define MEMCACHE_BINARY_H

#include "common.h"

/* Binary protocol magic bytes. */
#define MC_BINARY_REQUEST			0x80
#define MC_BINARY_RESPONSE			0x81

/* Binary protocol command codes. */
#define MC_BINARY_OPCODE_GET			0x00
#define MC_BINARY_OPCODE_SET			0x01
#define MC_BINARY_OPCODE_ADD			0x02
#define MC_BINARY_OPCODE_REPLACE		0x03
#define MC_BINARY_OPCODE_DELETE			0x04
#define MC_BINARY_OPCODE_INCREMENT		0x05
#define MC_BINARY_OPCODE_DECREMENT		0x06
#define MC_BINARY_OPCODE_QUIT			0x07
#define MC_BINARY_OPCODE_FLUSH			0x08
#define MC_BINARY_OPCODE_GETQ			0x09
#define MC_BINARY_OPCODE_NOOP			0x0a
#define MC_BINARY_OPCODE_VERSION		0x0b
#define MC_BINARY_OPCODE_GETK			0x0c
#define MC_BINARY_OPCODE_GETKQ			0x0d
#define MC_BINARY_OPCODE_APPEND			0x0e
#define MC_BINARY_OPCODE_PREPEND		0x0f
#define MC_BINARY_OPCODE_STAT			0x10
#define MC_BINARY_OPCODE_SETQ			0x11
#define MC_BINARY_OPCODE_ADDQ			0x12
#define MC_BINARY_OPCODE_REPLACEQ		0x13
#define MC_BINARY_OPCODE_DELETEQ		0x14
#define MC_BINARY_OPCODE_INCREMENTQ		0x15
#define MC_BINARY_OPCODE_DECREMENTQ		0x16
#define MC_BINARY_OPCODE_QUITQ			0x17
#define MC_BINARY_OPCODE_FLUSHQ			0x18
#define MC_BINARY_OPCODE_APPENDQ		0x19
#define MC_BINARY_OPCODE_PREPENDQ		0x1a

/* Binary protocol response status codes. */
#define MC_BINARY_STATUS_NO_ERROR		0x00
#define MC_BINARY_STATUS_KEY_NOT_FOUND		0x01
#define MC_BINARY_STATUS_KEY_EXISTS		0x02
#define MC_BINARY_STATUS_VALUE_TOO_LARGE	0x03
#define MC_BINARY_STATUS_INVALID_ARGUMENTS	0x04
#define MC_BINARY_STATUS_ITEM_NOT_STORED	0x05
#define MC_BINARY_STATUS_NON_NUMERIC_VALUE	0x06
#define MC_BINARY_STATUS_UNKNOWN_COMMAND	0x81
#define MC_BINARY_STATUS_OUT_OF_MEMORY		0x82

/* Forward declarations. */
struct mc_state;
struct mc_parser;
struct mc_command;

bool __attribute__((nonnull(1)))
mc_binary_parse(struct mc_parser *parser);

void __attribute__((nonnull(1, 2)))
mc_binary_status(struct mc_state *state,
		 struct mc_command *command,
		 uint16_t status);

#endif /* MEMCACHE_BINARY_H */
