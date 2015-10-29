/*
 * base/memory/chunk.c - MainMemory chunks.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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

#include "base/memory/chunk.h"

#include "base/base.h"
#include "base/log/error.h"
#include "base/log/plain.h"
#include "base/memory/memory.h"
#include "base/thread/backoff.h"
#include "base/thread/domain.h"
#include "base/thread/thread.h"
#include "base/util/exit.h"

#define MM_CHUNK_FLUSH_THRESHOLD	(64)
#define MM_CHUNK_ERROR_THRESHOLD	(512)
#define MM_CHUNK_FATAL_THRESHOLD	(4096)

/**********************************************************************
 * Chunk Allocation and Reclamation.
 **********************************************************************/

struct mm_chunk * __attribute__((malloc))
mm_chunk_create_global(size_t size)
{
	size += sizeof(struct mm_chunk);
	struct mm_chunk *chunk = mm_global_alloc(size);
	chunk->base.tag = MM_CHUNK_GLOBAL;
	mm_slink_prepare(&chunk->base.slink);
	return chunk;
}

struct mm_chunk *__attribute__((malloc))
mm_chunk_create_common(size_t size)
{
	size += sizeof(struct mm_chunk);
	struct mm_chunk *chunk = mm_common_alloc(size);
	chunk->base.tag = MM_CHUNK_COMMON;
	mm_slink_prepare(&chunk->base.slink);
	return chunk;
}

struct mm_chunk * __attribute__((malloc))
mm_chunk_create_regular(size_t size)
{
	size += sizeof(struct mm_chunk);
	struct mm_chunk *chunk = mm_regular_alloc(size);
	chunk->base.tag = MM_CHUNK_REGULAR;
	mm_slink_prepare(&chunk->base.slink);
	return chunk;
}

struct mm_chunk * __attribute__((malloc))
mm_chunk_create_private(size_t size)
{
	size += sizeof(struct mm_chunk);
	struct mm_chunk *chunk = mm_private_alloc(size);
	chunk->base.tag = mm_thread_self();
	mm_slink_prepare(&chunk->base.slink);
	return chunk;
}

struct mm_chunk * __attribute__((malloc))
mm_chunk_create(size_t size)
{
	// Prefer private space if available.
#if ENABLE_SMP
	struct mm_private_space *space = mm_private_space_get();
	if (mm_private_space_ready(space))
		return mm_chunk_create_private(size);
#else
	struct mm_domain *domain = mm_domain_selfptr();
	if (domain == mm_regular_domain && mm_private_space_ready(&mm_regular_space))
		return mm_chunk_create_regular(size);
#endif

	// Common space could only be used after it gets
	// initialized during bootstrap.
	if (likely(mm_common_space_ready()))
		return mm_chunk_create_common(size);

	// Use global allocator if everything else fails
	// (that is during bootstrap and shutdown).
	return mm_chunk_create_global(size);
}

void __attribute__((nonnull(1)))
mm_chunk_destroy(struct mm_chunk *chunk)
{
	mm_chunk_t tag = mm_chunk_gettag(chunk);

	// A chunk from a shared memory space can be freed by any thread in
	// the same manner utilizing synchronization mechanisms built-in to
	// the corresponding memory allocation routines.
	if (tag == MM_CHUNK_COMMON) {
		mm_common_free(chunk);
		return;
	}
	if (unlikely(tag == MM_CHUNK_GLOBAL)) {
		mm_global_free(chunk);
		return;
	}

	if (tag == MM_CHUNK_REGULAR) {
#if ENABLE_SMP
		// In SMP mode regular memory space is just another case of
		// shared space with built-in synchronization. So it can be
		// freed by any thread alike.
		mm_regular_free(chunk);
		return;
#else
		struct mm_domain *domain = mm_domain_selfptr();
		if (domain == mm_regular_domain) {
			mm_regular_free(chunk);
			return;
		}
#endif
	}

	// A chunk from a private space can be immediately freed by its
	// originating thread but it is a subject for asynchronous memory
	// reclamation mechanism for any other thread.
	struct mm_thread *thread = mm_thread_selfptr();
	struct mm_domain *domain = mm_thread_getdomain(thread);
	if (domain == mm_regular_domain && tag == mm_thread_getnumber(thread)) {
		mm_private_free(chunk);
		return;
	}

	thread->deferred_chunks_count++;
	mm_stack_insert(&thread->deferred_chunks, &chunk->base.slink);
	mm_chunk_enqueue_deferred(thread, false);
}

void __attribute__((nonnull(1)))
mm_chunk_destroy_stack(struct mm_stack *stack)
{
	struct mm_slink *link = mm_stack_head(stack);
	while (link != NULL) {
		struct mm_slink *next = link->next;
		mm_chunk_destroy(mm_chunk_from_slink(link));
		link = next;
	}
}

void __attribute__((nonnull(1)))
mm_chunk_destroy_queue(struct mm_queue *queue)
{
	struct mm_qlink *link = mm_queue_head(queue);
	while (link != NULL) {
		struct mm_qlink *next = link->next;
		mm_chunk_destroy(mm_chunk_from_qlink(link));
		link = next;
	}
}

static void
mm_chunk_free_req(uintptr_t context __mm_unused__, uintptr_t *arguments)
{
	struct mm_chunk *chunk = (struct mm_chunk *) arguments[0];
	mm_private_free(chunk);
}

void __attribute__((nonnull(1)))
mm_chunk_enqueue_deferred(struct mm_thread *thread, bool flush)
{
	if (!flush && thread->deferred_chunks_count < MM_CHUNK_FLUSH_THRESHOLD)
		return;

	// Capture all the deferred chunks.
	struct mm_stack chunks = thread->deferred_chunks;
	mm_stack_prepare(&thread->deferred_chunks);
	thread->deferred_chunks_count = 0;

	// Try to submit the chunks to respective reclamation queues.
	while (!mm_stack_empty(&chunks)) {
		struct mm_slink *link = mm_stack_remove(&chunks);
		struct mm_chunk *chunk = containerof(link, struct mm_chunk, base.slink);

		struct mm_domain *domain = mm_regular_domain;
#if ENABLE_SMP
		mm_chunk_t tag = mm_chunk_gettag(chunk);
		struct mm_thread *origin = mm_domain_getthread(domain, tag);
#else
		struct mm_thread *origin = mm_domain_getthread(domain, 0);
#endif
		uint32_t backoff = 0;
		while (!mm_thread_trysend_1(origin, mm_chunk_free_req,
					    (uintptr_t) chunk)) {
			if (backoff == 0) {
				// Wake up a possibly sleeping origin thread.
				mm_thread_notify(origin);
			} else if (backoff >= MM_BACKOFF_SMALL) {
				// If failed to submit the chunk after a number
				// of attempts then defer it again.
				mm_stack_insert(&thread->deferred_chunks, link);
				thread->deferred_chunks_count++;
				break;
			}
			backoff = mm_thread_backoff(backoff);
		}
	}

	// Let know if chunk reclamation consistently has problems.
	if (thread->deferred_chunks_count > MM_CHUNK_ERROR_THRESHOLD) {
		if (thread->deferred_chunks_count < MM_CHUNK_FATAL_THRESHOLD)
			mm_error(0, "Problem with chunk reclamation");
		else
			mm_fatal(0, "Problem with chunk reclamation");
	}
}
