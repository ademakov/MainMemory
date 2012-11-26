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

void
mm_port_init(void)
{
}

void
mm_port_free(void)
{
}

struct mm_port *
mm_port_create(struct mm_task *task)
{
	ENTER();

	struct mm_port *port = mm_alloc(sizeof(struct mm_port));
	port->task = task;
	port->start = 0;
	port->count = 0;

	mm_list_insert_tail(&task->ports, &port->ports);

	LEAVE();
	return port;
}

void
mm_port_destroy(struct mm_port *port)
{
	ENTER();

	mm_list_delete(&port->ports);
	mm_free(port);

	LEAVE();
}

int
mm_port_send(struct mm_port *port, uint32_t *start, uint32_t count)
{
	ENTER();
	ASSERT(count <= MM_PORT_SIZE);

	int rc = 0;
	if (unlikely((port->count + count) > MM_PORT_SIZE)) {
		rc = -1;
		errno = EAGAIN;
		goto done;
	}

	uint32_t ring_end = (port->start + port->count) % MM_PORT_SIZE;
	uint32_t *ring_ptr = &port->ring[ring_end];

	port->count += count;

	if ((ring_end + count) > MM_PORT_SIZE) {
		uint32_t top_count = MM_PORT_SIZE - ring_end;
		count -= top_count;
		while (top_count--) {
			*ring_ptr++ = *start++;
		}

		ring_ptr = &port->ring[0];
	}

	while (count--) {
		*ring_ptr++ = *start++;
	}

done:
	LEAVE();
	return rc;
}

int
mm_port_receive(struct mm_port *port, uint32_t *start, uint32_t count)
{
	ENTER();
	ASSERT(count <= MM_PORT_SIZE);

	int rc = 0;
	if (port->count < count) {
		rc = -1;
		errno = EAGAIN;
		goto done;
	}

	uint32_t *ring_ptr = &port->ring[port->start];

	port->count -= count;

	if ((port->start + count) > MM_PORT_SIZE) {
		uint32_t top_count = MM_PORT_SIZE - port->start;
		count -= top_count;
		while (top_count--) {
			*start++ = *ring_ptr++;
		}

		ring_ptr = &port->ring[0];
		port->start = 0;
	}

	port->start += count;
	while (count--) {
		*start++ = *ring_ptr++;
	}

done:
	LEAVE();
	return rc;
}
