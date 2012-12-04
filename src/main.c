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

#include "common.h"
#include "event.h"
#include "net.h"
#include "port.h"
#include "sched.h"
#include "task.h"
#include "util.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static struct mm_net_server *u_cmd_server;
static struct mm_net_server *i_cmd_server;

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

static void
mm_read_ready(struct mm_net_client *cli)
{
	char buf[1026];
	int n = read(cli->sock, buf, sizeof(buf));
	if (n <= 0) {
		if (n < 0)
			mm_error(errno, "read()");
		mm_event_unregister_fd(cli->sock);
		close(cli->sock);
	}
}

static void
mm_write_ready(struct mm_net_client *cli)
{
	write(cli->sock, "test\n", 5);

	mm_net_close(cli);
}

struct mm_net_proto cmd_proto = {
	.accept = NULL,
	.read_ready = mm_read_ready,
	.write_ready = mm_write_ready,
	.cleanup = NULL,
};

static void
mm_init(void)
{
	ENTER();

	/* Initialize subsystems. */
	mm_signal_init();
	mm_task_init();
	mm_port_init();
	mm_sched_init();
	mm_event_init();
	mm_net_init();

	LEAVE();
}

static void
mm_term(void)
{
	ENTER();

	/* Terminate subsystems. */
	mm_net_term();
	mm_event_term();
	mm_sched_term();
	mm_task_term();
	mm_port_term();

	LEAVE();
}

static void
mm_server_open(void)
{
	ENTER();

	u_cmd_server = mm_net_create_unix_server("test", "mm_cmd.sock");
	i_cmd_server = mm_net_create_inet_server("test", "127.0.0.1", 8000);

	mm_net_start_server(u_cmd_server, &cmd_proto);
	mm_net_start_server(i_cmd_server, &cmd_proto);

	LEAVE();
}

static void
mm_server_close(void)
{
	ENTER();

	mm_net_stop_server(u_cmd_server);
	mm_net_stop_server(i_cmd_server);

	LEAVE();
}

int
main(int ac, char *av[])
{
	ENTER();

	/* Initialize. */
	mm_init();

	/* Start server. */
	mm_server_open();

	/* Execute main loop. */
	mm_event_start();
	mm_sched_start();

	/* Shutdown server. */
	mm_server_close(); 

	/* Terminate. */
	mm_term();

	LEAVE();
	return EXIT_SUCCESS;
}
