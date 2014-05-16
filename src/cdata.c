/*
 * cdata.c - MainMemory core-local data.
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

#include "cdata.h"

#include "alloc.h"
#include "bits.h"
#include "core.h"
#include "list.h"
#include "log.h"
#include "trace.h"
#include "util.h"

#define MM_CDATA_ALIGN		(16)
#define MM_CDATA_CHUNK_HEAD	mm_align(sizeof(struct mm_cdata_chunk), MM_CDATA_ALIGN)

// CData entry info.
struct mm_cdata_entry
{
	const char *name;
	struct mm_list link;
	mm_cdata_t dref;
	size_t size;
};

// CData chunk data.
struct mm_cdata_chunk
{
	size_t used;
	struct mm_list chunk_link;
	struct mm_list entry_list;
};

static struct mm_list mm_cdata_chunk_list;

static struct mm_cdata_chunk *
mm_cdata_add_chunk(void)
{
	ENTER();

	size_t size = MM_CDATA_CHUNK_HEAD + mm_core_getnum() * MM_CDATA_CHUNK_SIZE;

	struct mm_cdata_chunk *chunk = mm_global_alloc(size);
	chunk->used = 0;
	mm_list_init(&chunk->entry_list);

	mm_list_append(&mm_cdata_chunk_list, &chunk->chunk_link);

	LEAVE();
	return chunk;
}

void
mm_cdata_init(void)
{
	ENTER();

	mm_list_init(&mm_cdata_chunk_list);
	mm_cdata_add_chunk();

	LEAVE();
}

void
mm_cdata_term(void)
{
	ENTER();

	LEAVE();
}

mm_cdata_t
mm_cdata_alloc(const char *name, size_t size)
{
	ENTER();
	ASSERT(size > 0);
	ASSERT(size <= MM_CDATA_CHUNK_SIZE);

	size = mm_align(size, MM_CDATA_ALIGN);

	struct mm_list *link = mm_list_tail(&mm_cdata_chunk_list);
	struct mm_cdata_chunk *chunk = containerof(link, struct mm_cdata_chunk, chunk_link);
	if ((chunk->used + size) > MM_CDATA_CHUNK_SIZE)
		chunk = mm_cdata_add_chunk();

	struct mm_cdata_entry *entry = mm_global_alloc(sizeof(struct mm_cdata_entry));
	entry->name = mm_strdup(&mm_alloc_global, name);
	entry->dref = (mm_cdata_t) chunk + chunk->used + MM_CDATA_CHUNK_HEAD;
	entry->size = size;

	mm_list_append(&chunk->entry_list, &entry->link);
	chunk->used += size;

	LEAVE();
	return entry->dref;
}

void
mm_cdata_summary(void)
{
	int nchunks = 0;
	int nentries = 0;
	size_t used = 0;

	struct mm_list *chunk_link = &mm_cdata_chunk_list;
	while (chunk_link != mm_list_tail(&mm_cdata_chunk_list)) {
		nchunks++;
		chunk_link = chunk_link->next;
		struct mm_cdata_chunk *chunk = containerof(chunk_link, struct mm_cdata_chunk, chunk_link);

		struct mm_list *entry_link = &chunk->entry_list;
		while (entry_link != mm_list_tail(&chunk->entry_list)) {
			nentries++;
			entry_link = entry_link->next;
			struct mm_cdata_entry *entry = containerof(entry_link, struct mm_cdata_entry, link);
			used += entry->size;

			mm_verbose("core local data entry (%s): %ld bytes",
				   entry->name, (long) entry->size);
		}
	}

	mm_brief("core local data summary: %d chunk(s) of %d bytes "
		 "with %d entries using up %ld bytes",
		 nchunks, MM_CDATA_CHUNK_SIZE, nentries, (long) used);
}
