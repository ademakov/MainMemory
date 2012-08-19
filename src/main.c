/*
 * daemon.c - MainMemory main routine.
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

#include <event.h>
#include <util.h>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static void
mm_term_handler(int signo __attribute__((unused)))
{
	ENTER();

	mm_event_stop();

	LEAVE();
}

static void
mm_signal(int signo, void (*handler)(int))
{
	ENTER();

	struct sigaction sa;
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(signo, &sa, NULL) == -1) {
		mm_fatal(errno, "Failed sigaction() call");
	}

	LEAVE();
}

static void
mm_signal_init()
{
	ENTER();

	mm_signal(SIGINT, mm_term_handler);
	mm_signal(SIGTERM, mm_term_handler);

	LEAVE();
}

int
main(int ac, char *av[])
{
	ENTER();

	/* initialize */
	mm_signal_init();
	mm_event_init();

	/* main loop */
	mm_event_loop();

	/* free resources */
	mm_event_free();

	LEAVE();
	return EXIT_SUCCESS;
}
