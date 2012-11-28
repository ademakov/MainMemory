/*
 * port.h - MainMemory ports.
 *
 * Copyright (C) 2012  Aleksey Demakov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef PORT_H
#define PORT_H

#include "common.h"
#include "list.h"

/* Forward declaration. */
struct mm_task;

/* */
#define MM_PORT_SIZE	248

struct mm_port
{
	/* The port owner. */
	struct mm_task *task;

	/* A link in the task's ports list. */
	struct mm_list ports;

	/* The tasks blocked on the port send. */
	struct mm_list blocked_senders;

	/* Message buffer. */
	uint16_t start;
	uint16_t count;
	uint32_t ring[MM_PORT_SIZE];
};

void mm_port_init(void);
void mm_port_term(void);

struct mm_port * mm_port_create(struct mm_task *task)
	__attribute__((nonnull(1)));

void mm_port_destroy(struct mm_port *port)
	__attribute__((nonnull(1)));

int mm_port_send(struct mm_port *port, uint32_t *start, uint32_t count)
	__attribute__((nonnull(1, 2)));

int mm_port_receive(struct mm_port *port, uint32_t *start, uint32_t count)
	__attribute__((nonnull(1, 2)));

#endif /* PORT_H */
