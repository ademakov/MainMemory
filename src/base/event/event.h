/*
 * base/event/event.h - MainMemory event loop.
 *
 * Copyright (C) 2012-2015  Aleksey Demakov
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

#ifndef BASE_EVENT_EVENT_H
#define BASE_EVENT_EVENT_H

#include "common.h"
#include "base/list.h"

/* Event types. */
typedef enum {
	MM_EVENT_NONE = -1,
	MM_EVENT_INPUT,
	MM_EVENT_OUTPUT,
	MM_EVENT_INPUT_ERROR,
	MM_EVENT_OUTPUT_ERROR,
	MM_EVENT_ATTACH,
	MM_EVENT_DISABLE,
	MM_EVENT_RECLAIM,
} mm_event_t;

typedef enum {
	MM_EVENT_IGNORED,
	MM_EVENT_REGULAR,
	MM_EVENT_ONESHOT,
} mm_event_occurrence_t;

typedef enum {
	MM_EVENT_LOOSE,
	MM_EVENT_BOUND,
	MM_EVENT_AGILE,
} mm_event_affinity_t;

/**********************************************************************
 * Event subsystem initialization.
 **********************************************************************/

void mm_event_init(void);

/**********************************************************************
 * Event subsystem statistics.
 **********************************************************************/

void mm_event_stats(void);

/**********************************************************************
 * Event handlers.
 **********************************************************************/

/* Event handler identifier. */
typedef uint8_t mm_event_hid_t;

/* Event handler routine. */
typedef void (*mm_event_handler_t)(mm_event_t event, void *data);

/* Event handler descriptor. */
struct mm_event_hdesc
{
	mm_event_handler_t handler;
};

mm_event_hid_t mm_event_register_handler(mm_event_handler_t handler);

/**********************************************************************
 * I/O events support.
 **********************************************************************/

typedef union {
	struct {
		union {
			struct {
				uint8_t ready;
				uint8_t error;
			};
			uint16_t state;
		} input;
		union {
			struct {
				uint8_t ready;
				uint8_t error;
			};
			uint16_t state;
		} output;
	};
	uint32_t state;
} mm_event_iostate_t;

/* Event sink. */
struct mm_event_fd
{
	/* The file descriptor to watch. */
	int fd;

	/* The thread the owns the sink. */
	mm_thread_t target;

	/* Event handers. */
	mm_event_hid_t handler;

	/* Immutable flags. */
	unsigned loose_target : 1;
	unsigned bound_target : 1;
	unsigned regular_input : 1;
	unsigned oneshot_input : 1;
	unsigned regular_output : 1;
	unsigned oneshot_output : 1;

	/* The event sink I/O state. */
	mm_event_iostate_t io;

	/* The unregister state. */
	mm_event_t unregister_phase;

	/* Flags used by the poller thread. */
	unsigned changed : 1;
	unsigned oneshot_input_trigger : 1;
	unsigned oneshot_output_trigger : 1;

	/* Flags used by the owner thread. */
	uint8_t attached;

	/* Reclaim queue link. */
	struct mm_slink reclaim_link;
};

bool NONNULL(1)
mm_event_prepare_fd(struct mm_event_fd *sink, int fd, mm_event_hid_t handler,
		    mm_event_occurrence_t input_mode, mm_event_occurrence_t output_mode,
		    mm_event_affinity_t target);

void NONNULL(1)
mm_event_convey(struct mm_event_fd *sink);

void NONNULL(1)
mm_event_detach(struct mm_event_fd *sink);

static inline mm_thread_t NONNULL(1)
mm_event_target(const struct mm_event_fd *sink)
{
	return sink->target;
}

static inline bool NONNULL(1)
mm_event_attached(const struct mm_event_fd *sink)
{
	return sink->attached;
}

#endif /* BASE_EVENT_EVENT_H */
