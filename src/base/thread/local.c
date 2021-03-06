/*
 * base/thread/local.c - MainMemory thread-local data.
 *
 * Copyright (C) 2014-2015  Aleksey Demakov
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

#include "base/thread/local.h"

#include "base/bitops.h"
#include "base/list.h"
#include "base/lock.h"
#include "base/report.h"
#include "base/memory/alloc.h"
#include "base/thread/domain.h"
#include "base/util/libcall.h"

#define MM_THREAD_LOCAL_ALIGN		(8)

#define MM_THREAD_LOCAL_CHUNK_HEAD	\
	mm_round_up(sizeof(struct mm_thread_local_chunk), MM_THREAD_LOCAL_ALIGN)

// Thread local chunk info.
struct mm_thread_local_chunk
{
	struct mm_qlink link;
	size_t used;
};

// Thread local entry info.
struct mm_thread_local_entry
{
	struct mm_qlink link;
	const char *name;
	size_t size;
	mm_thread_local_t base;
};

static struct mm_thread_local_chunk *
mm_thread_local_create_chunk(struct mm_domain *domain)
{
	size_t size = MM_THREAD_LOCAL_CHUNK_HEAD + domain->nthreads * MM_THREAD_LOCAL_CHUNK_SIZE;

	struct mm_thread_local_chunk *chunk = mm_memory_fixed_xalloc(size);
	chunk->used = 0;

	return chunk;
}

void NONNULL(1)
mm_thread_local_init(struct mm_domain *domain)
{
	// Initialize lists.
	mm_queue_prepare(&domain->per_thread_chunk_list);
	mm_queue_prepare(&domain->per_thread_entry_list);

	domain->per_thread_lock = (mm_lock_t) MM_LOCK_INIT;

	// Provision the first chunk w/o locking.
	struct mm_thread_local_chunk *chunk = mm_thread_local_create_chunk(domain);
	mm_queue_append(&domain->per_thread_chunk_list, &chunk->link);
}

void NONNULL(1)
mm_thread_local_term(struct mm_domain *domain)
{
	// Release all data info entries.
	while (!mm_queue_empty(&domain->per_thread_entry_list)) {
		struct mm_qlink *link = mm_queue_remove(&domain->per_thread_entry_list);
		struct mm_thread_local_entry *entry = containerof(link, struct mm_thread_local_entry, link);
		mm_memory_free(entry);
	}

	// Release all data chunks.
	while (!mm_queue_empty(&domain->per_thread_chunk_list)) {
		struct mm_qlink *link = mm_queue_remove(&domain->per_thread_chunk_list);
		struct mm_thread_local_chunk *chunk = containerof(link, struct mm_thread_local_chunk, link);
		mm_memory_free(chunk);
	}
}

mm_thread_local_t NONNULL(1)
mm_thread_local_alloc(struct mm_domain *domain, const char *name, size_t size)
{
	ASSERT(size > 0);
	ASSERT(size <= MM_THREAD_LOCAL_CHUNK_SIZE);

	// Allocate the info entry.
	struct mm_thread_local_entry *entry = mm_memory_fixed_xalloc(sizeof(struct mm_thread_local_entry));
	entry->name = mm_memory_strdup(name);
	entry->size = size;

	// Round the size to maintain the required alignment.
	size = mm_round_up(size, MM_THREAD_LOCAL_ALIGN);

	mm_global_lock(&domain->per_thread_lock);

	// Find out a chunk with sufficient free space.
	struct mm_qlink *link = mm_queue_head(&domain->per_thread_chunk_list);
	struct mm_thread_local_chunk *chunk = containerof(link, struct mm_thread_local_chunk, link);
	while ((chunk->used + size) > MM_THREAD_LOCAL_CHUNK_SIZE) {
		link = link->next;
		if (link == NULL)
			break;
		chunk = containerof(link, struct mm_thread_local_chunk, link);
	}

	// Add a new chunk if not found.
	struct mm_thread_local_chunk *discard_chunk = NULL;
	if (link == NULL) {
		// Allocate a new chunk.
		mm_global_unlock(&domain->per_thread_lock);
		discard_chunk = mm_thread_local_create_chunk(domain);
		mm_global_lock(&domain->per_thread_lock);

		// Check to see if there is a concurrently added chunk
		// (a chance that more than one chunk has been added is
		// absolutely negligible).
		link = mm_queue_tail(&domain->per_thread_chunk_list);
		chunk = containerof(link, struct mm_thread_local_chunk, link);
		if ((chunk->used + size) > MM_THREAD_LOCAL_CHUNK_SIZE) {
			// Seems no, so use the allocated chunk.
			chunk = discard_chunk;
			discard_chunk = NULL;
			mm_queue_append(&domain->per_thread_chunk_list, &chunk->link);
		}
	}

	// Allocate the space for data.
	entry->base = (mm_thread_local_t) chunk + chunk->used + MM_THREAD_LOCAL_CHUNK_HEAD;
	chunk->used += size;

	// Make the data info globally visible.
	mm_queue_append(&domain->per_thread_entry_list, &entry->link); 

	mm_global_unlock(&domain->per_thread_lock);

	// Release the discarded chunk at last.
	if (discard_chunk != NULL)
		mm_memory_free(discard_chunk);

	return entry->base;
}

/* This function is not thread-safe, and that should be okay so far. */
void NONNULL(1)
mm_thread_local_summary(struct mm_domain *domain)
{
	int nchunks = 0;
	int nentries = 0;
	size_t used = 0;

	struct mm_qlink *chunk_link = mm_queue_head(&domain->per_thread_chunk_list);
	for (; chunk_link != NULL; chunk_link = chunk_link->next) {
		struct mm_thread_local_chunk *chunk = containerof(chunk_link, struct mm_thread_local_chunk, link);
		used += chunk->used;
		nchunks++;
	}

	struct mm_qlink *entry_link = mm_queue_head(&domain->per_thread_entry_list);
	for (; entry_link != NULL; entry_link = entry_link->next) {
		struct mm_thread_local_entry *entry = containerof(entry_link, struct mm_thread_local_entry, link);
		mm_verbose("thread local data entry (%s): %lu bytes",
			   entry->name, (unsigned long) entry->size);
		nentries++;
	}

	mm_brief("thread local data summary: %d chunk(s) of %d bytes "
		 "with %d entries using up %lu bytes",
		 nchunks, MM_THREAD_LOCAL_CHUNK_SIZE, nentries, (unsigned long) used);
}
