/*
 * core/port.c - MainMemory ports.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#include "core/port.h"
#include "core/core.h"
#include "core/task.h"

#include "base/log/trace.h"
#include "base/memory/global.h"

void
mm_port_init(void)
{
}

void
mm_port_term(void)
{
}

struct mm_port * NONNULL(1)
mm_port_create(struct mm_task *task)
{
	ENTER();

	struct mm_port *port = mm_global_alloc(sizeof(struct mm_port));
	port->lock = (mm_regular_lock_t) MM_REGULAR_LOCK_INIT;
	port->task = task;
	port->start = 0;
	port->count = 0;
	mm_waitset_prepare(&port->blocked_senders);

	mm_list_append(&task->ports, &port->ports);

	LEAVE();
	return port;
}

void NONNULL(1)
mm_port_destroy(struct mm_port *port)
{
	ENTER();

	mm_list_delete(&port->ports);
	mm_global_free(port);

	LEAVE();
}

static int
mm_port_send_internal(struct mm_port *port, uint32_t *start, uint32_t count, bool blocking)
{
	ENTER();
	ASSERT(count <= (MM_PORT_SIZE / 2));
	ASSERT(port->task != mm_task_selfptr());
	int rc = 0;

again:
	mm_regular_lock(&port->lock);
	if (unlikely((port->count + count) > MM_PORT_SIZE)) {
		if (blocking) {
			mm_waitset_wait(&port->blocked_senders, &port->lock);
			mm_task_testcancel();
			goto again;
		} else {
			mm_regular_unlock(&port->lock);
			rc = -1;
			goto leave;
		}
	}

	uint32_t ring_end = (port->start + port->count) % MM_PORT_SIZE;
	uint32_t *ring_ptr = &port->ring[ring_end];

	port->count += count;

	if (unlikely((ring_end + count) > MM_PORT_SIZE)) {
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

	mm_regular_unlock(&port->lock);
	mm_core_run_task(port->task);

leave:
	LEAVE();
	return rc;
}

static int
mm_port_receive_internal(struct mm_port *port, uint32_t *start, uint32_t count, bool blocking)
{
	ENTER();
	ASSERT(count <= (MM_PORT_SIZE / 2));
	ASSERT(port->task == mm_task_selfptr());
	int rc = 0;

again:
	mm_regular_lock(&port->lock);
	if (port->count < count) {
		mm_regular_unlock(&port->lock);
		if (blocking) {
			mm_task_block();
			mm_task_testcancel();
			goto again;
		} else {
			rc = -1;
			goto leave;
		}
	}

	uint32_t *ring_ptr = &port->ring[port->start];

	port->count -= count;

	if (unlikely((port->start + count) > MM_PORT_SIZE)) {
		uint32_t top_count = MM_PORT_SIZE - port->start;
		count -= top_count;
		while (top_count--) {
			*start++ = *ring_ptr++;
		}

		ring_ptr = &port->ring[0];
		port->start = 0;
	}

	port->start = (port->start + count) % MM_PORT_SIZE;
	while (count--) {
		*start++ = *ring_ptr++;
	}

	mm_waitset_broadcast(&port->blocked_senders, &port->lock);

leave:
	LEAVE();
	return rc;
}


int NONNULL(1, 2)
mm_port_send(struct mm_port *port, uint32_t *start, uint32_t count)
{
	ENTER();

	int rc = mm_port_send_internal(port, start, count, false);

	LEAVE();
	return rc;
}

void NONNULL(1, 2)
mm_port_send_blocking(struct mm_port *port, uint32_t *start, uint32_t count)
{
	ENTER();

	mm_port_send_internal(port, start, count, true);

	LEAVE();
}

int NONNULL(1, 2)
mm_port_receive(struct mm_port *port, uint32_t *start, uint32_t count)
{
	ENTER();

	int rc = mm_port_receive_internal(port, start, count, false);

	LEAVE();
	return rc;
}

void NONNULL(1, 2)
mm_port_receive_blocking(struct mm_port *port, uint32_t *start, uint32_t count)
{
	ENTER();

	mm_port_receive_internal(port, start, count, true);

	LEAVE();
}
