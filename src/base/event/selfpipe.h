/*
 * base/event/selfpipe.h - MainMemory self-pipe trick.
 *
 * Copyright (C) 2013-2020  Aleksey Demakov
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

#ifndef BASE_EVENT_SELFPIPE_H
#define BASE_EVENT_SELFPIPE_H

#include "common.h"

struct mm_selfpipe
{
	int read_fd;
	int write_fd;
	bool notified;
};

void NONNULL(1)
mm_selfpipe_prepare(struct mm_selfpipe *selfpipe);

void NONNULL(1)
mm_selfpipe_cleanup(struct mm_selfpipe *selfpipe);

static inline bool NONNULL(1)
mm_selfpipe_is_notified(const struct mm_selfpipe *selfpipe)
{
	return selfpipe->notified;
}

static inline void NONNULL(1)
mm_selfpipe_set_notified(struct mm_selfpipe *selfpipe)
{
	selfpipe->notified = true;
}

void NONNULL(1)
mm_selfpipe_notify(struct mm_selfpipe *selfpipe);

void NONNULL(1)
mm_selfpipe_absorb(struct mm_selfpipe *selfpipe);

#endif /* BASE_EVENT_SELFPIPE_H */
