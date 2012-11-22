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

struct mm_task;

struct mm_port
{
	/* The port owner. */
	struct mm_task *task;

	/* A link in the task's ports list. */
	struct mm_list ports;
};

void mm_port_init(void);
void mm_port_free(void);

struct mm_port * mm_port_create(struct mm_task *task);
void mm_port_destroy(struct mm_port *port);

#endif /* PORT_H */
