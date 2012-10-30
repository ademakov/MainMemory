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

#include "event.h"
#include "net.h"
#include "sched.h"
#include "util.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static struct mm_net_server *u_cmd_server;
static struct mm_net_server *i_cmd_server;

static void
mm_exit_handler(void)
{
	ENTER();

	mm_net_exit();

	LEAVE();
}

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
mm_signal_init(void)
{
	ENTER();

	mm_signal(SIGINT, mm_term_handler);
	mm_signal(SIGTERM, mm_term_handler);

	LEAVE();
}

struct mm_net_proto cmd_proto;

static void
mm_server_open(void)
{
	ENTER();

	mm_net_init();
	u_cmd_server = mm_net_create_unix_server("mm_cmd.sock");
	i_cmd_server = mm_net_create_inet_server("127.0.0.1", 8000);

	mm_net_set_server_proto(u_cmd_server, &cmd_proto, 0);
	mm_net_set_server_proto(i_cmd_server, &cmd_proto, 0);

	mm_net_start_server(u_cmd_server);
	mm_net_start_server(i_cmd_server);

	LEAVE();
}

static void
mm_server_close(void)
{
	ENTER();

	mm_net_stop_server(u_cmd_server);
	mm_net_stop_server(i_cmd_server);
	mm_net_free();

	LEAVE();
}

int
main(int ac, char *av[])
{
	ENTER();

	/* Register exit cleanup handler. */
	atexit(&mm_exit_handler);

	/* Initialize. */
	mm_signal_init();
	mm_sched_init();
	mm_event_init();

	/* Open server sockets. */
	mm_server_open();

	/* Execute main loop. */
	mm_event_loop();

	/* Close server sockets. */
	mm_server_close(); 

	/* Free resources. */
	mm_event_free();
	mm_sched_free();

	LEAVE();
	return EXIT_SUCCESS;
}
