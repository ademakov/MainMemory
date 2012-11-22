/*
 * port.c - MainMemory ports.
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

#include "port.h"

#include "pool.h"
#include "sched.h"
#include "task.h"
#include "util.h"

static struct mm_pool mm_port_pool;

void
mm_port_init(void)
{
	ENTER();

	mm_pool_init(&mm_port_pool, sizeof (struct mm_port));

	LEAVE();
}

void
mm_port_free(void)
{
	ENTER();

	mm_pool_discard(&mm_port_pool);

	LEAVE();
}

struct mm_port *
mm_port_create(struct mm_task *task)
{
	ENTER();

	struct mm_port *port = mm_pool_alloc(&mm_port_pool);
	port->task = task;
	mm_list_insert_tail(&task->ports, &port->ports);

	LEAVE();
	return port;
}

void
mm_port_destroy(struct mm_port *port)
{
	ENTER();

	mm_list_delete(&port->ports);
	mm_pool_free(&mm_port_pool,&port);

	LEAVE();
}

