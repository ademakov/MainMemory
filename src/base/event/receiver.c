/*
 * base/event/receiver.c - MainMemory event receiver.
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

#include "base/event/receiver.h"

#include "base/event/batch.h"
#include "base/log/debug.h"
#include "base/log/trace.h"
#include "base/mem/memory.h"

void __attribute__((nonnull(1)))
mm_event_receiver_prepare(struct mm_event_receiver *receiver,
			  mm_thread_t ntargets)
{
	ENTER();

	receiver->events = mm_common_calloc(ntargets,
					    sizeof(struct mm_event_batch));
	for (mm_thread_t i = 0; i < ntargets; i++)
		mm_event_batch_prepare(&receiver->events[i]);

	mm_bitset_prepare(&receiver->targets, &mm_common_space.xarena,
			  ntargets);

	LEAVE();
}

void __attribute__((nonnull(1)))
mm_event_receiver_cleanup(struct mm_event_receiver *receiver)
{
	ENTER();

	for (mm_thread_t i = 0; i < mm_bitset_size(&receiver->targets); i++)
		mm_event_batch_cleanup(&receiver->events[i]);

	mm_bitset_cleanup(&receiver->targets, &mm_common_space.xarena);

	LEAVE();
}
