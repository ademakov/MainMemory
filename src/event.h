/*
 * event.h - MainMemory events.
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

#ifndef EVENT_H
#define	EVENT_H

#include <stdint.h>

#define MM_EVENT_READ	1
#define MM_EVENT_WRITE	2

void mm_event_init(void);
void mm_event_free(void);
void mm_event_loop(void);
void mm_event_stop(void);

typedef void (*mm_event_iocb)(int fd, int flags, intptr_t data);

int mm_event_verify_fd(int fd);
void mm_event_register_fd(int fd, int flags, mm_event_iocb iocb, intptr_t data);
void mm_event_unregister_fd(int fd);


#endif	/* EVENT_H */

