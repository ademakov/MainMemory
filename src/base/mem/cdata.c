/*
 * base/mem/cdata.c - MainMemory core-local data.
 *
 * Copyright (C) 2014  Aleksey Demakov
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

#include "base/mem/cdata.h"
#include "base/bitops.h"
#include "base/list.h"
#include "base/lock.h"
#include "base/log/debug.h"
#include "base/log/plain.h"
#include "base/mem/alloc.h"
#include "base/util/libcall.h"

#include "core/core.h"

#define MM_CDATA_ALIGN		(8)
#define MM_CDATA_CHUNK_HEAD	mm_round_up(sizeof(struct mm_cdata_chunk), MM_CDATA_ALIGN)

// CData chunk info.
struct mm_cdata_chunk
{
	struct mm_link link;

	size_t used;
};

// CData entry info.
struct mm_cdata_entry
{
	struct mm_link link;
	const char *name;
	size_t size;
	mm_cdata_t base;
};

static mm_lock_t mm_cdata_lock = MM_LOCK_INIT;
static struct mm_queue mm_cdata_chunk_list;
static struct mm_queue mm_cdata_entry_list;

static struct mm_cdata_chunk *
mm_cdata_create_chunk(void)
{
	size_t size = MM_CDATA_CHUNK_HEAD + mm_core_getnum() * MM_CDATA_CHUNK_SIZE;

	struct mm_cdata_chunk *chunk = mm_global_alloc(size);
	chunk->used = 0;

	return chunk;
}

void
mm_cdata_init(void)
{
	// Initialize lists.
	mm_queue_init(&mm_cdata_chunk_list);
	mm_queue_init(&mm_cdata_entry_list);

	// Provision the first chunk w/o locking.
	struct mm_cdata_chunk *chunk = mm_cdata_create_chunk();
	mm_queue_append(&mm_cdata_chunk_list, &chunk->link);
}

void
mm_cdata_term(void)
{
	// Release all data info entries.
	while (!mm_queue_empty(&mm_cdata_entry_list)) {
		struct mm_link *link = mm_queue_delete_head(&mm_cdata_entry_list);
		struct mm_cdata_entry *entry = containerof(link, struct mm_cdata_entry, link);
		mm_global_free(entry);
	}

	// Release all data chunks.
	while (!mm_queue_empty(&mm_cdata_chunk_list)) {
		struct mm_link *link = mm_queue_delete_head(&mm_cdata_chunk_list);
		struct mm_cdata_chunk *chunk = containerof(link, struct mm_cdata_chunk, link);
		mm_global_free(chunk);
	}
}

mm_cdata_t
mm_cdata_alloc(const char *name, size_t size)
{
	ASSERT(size > 0);
	ASSERT(size <= MM_CDATA_CHUNK_SIZE);

	// Allocate the info entry.
	struct mm_cdata_entry *entry = mm_global_alloc(sizeof(struct mm_cdata_entry));
	entry->name = mm_global_strdup(name);
	entry->size = size;

	// Round the size to maintain the required alignment.
	size = mm_round_up(size, MM_CDATA_ALIGN);

	mm_global_lock(&mm_cdata_lock);

	// Find out a chunk with sufficient free space.
	struct mm_link *link = mm_queue_head(&mm_cdata_chunk_list);
	struct mm_cdata_chunk *chunk = containerof(link, struct mm_cdata_chunk, link);
	while ((chunk->used + size) > MM_CDATA_CHUNK_SIZE) {
		link = link->next;
		if (link == NULL)
			break;
		chunk = containerof(link, struct mm_cdata_chunk, link);
	}

	// Add a new chunk if not found.
	struct mm_cdata_chunk *discard_chunk = NULL;
	if (link == NULL) {
		// Allocate a new chunk.
		mm_global_unlock(&mm_cdata_lock);
		struct mm_cdata_chunk *chunk = mm_cdata_create_chunk();
		mm_global_lock(&mm_cdata_lock);

		// Check to see if there is a concurrently added chunk
		// (a chance that more than one chunk has been added is
		// absolutely negligible).
		link = mm_queue_tail(&mm_cdata_chunk_list);
		struct mm_cdata_chunk *recheck_chunk = containerof(link, struct mm_cdata_chunk, link);
		if ((recheck_chunk->used + size) > MM_CDATA_CHUNK_SIZE) {
			// Seems no, so use the allocated chunk.
			mm_queue_append(&mm_cdata_chunk_list, &chunk->link);
		} else {
			// Yes, so discard the allocated chunk.
			discard_chunk = chunk;
			chunk = recheck_chunk;
		}
	}

	// Allocate the space for data.
	entry->base = (mm_cdata_t) chunk + chunk->used + MM_CDATA_CHUNK_HEAD;
	chunk->used += size;

	// Make the data info globally visible.
	mm_queue_append(&mm_cdata_entry_list, &entry->link); 

	mm_global_unlock(&mm_cdata_lock);

	// Release the discarded chunk at last.
	if (discard_chunk != NULL)
		mm_global_free(discard_chunk);

	return entry->base;
}

/* This function is not thread-safe, and that should be okay so far. */
void
mm_cdata_summary(void)
{
	int nchunks = 0;
	int nentries = 0;
	size_t used = 0;

	struct mm_link *chunk_link = mm_queue_head(&mm_cdata_chunk_list);
	for (; chunk_link != NULL; chunk_link = chunk_link->next) {
		struct mm_cdata_chunk *chunk = containerof(chunk_link, struct mm_cdata_chunk, link);
		used += chunk->used;
		nchunks++;
	}

	struct mm_link *entry_link = mm_queue_head(&mm_cdata_entry_list);
	for (; entry_link != NULL; entry_link = entry_link->next) {
		struct mm_cdata_entry *entry = containerof(entry_link, struct mm_cdata_entry, link);
		mm_verbose("core local data entry (%s): %lu bytes",
			   entry->name, (unsigned long) entry->size);
		nentries++;
	}

	mm_brief("core local data summary: %d chunk(s) of %d bytes "
		 "with %d entries using up %lu bytes",
		 nchunks, MM_CDATA_CHUNK_SIZE, nentries, (unsigned long) used);
}
