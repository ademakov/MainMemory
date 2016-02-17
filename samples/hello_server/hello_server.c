/*
 * hello_server.c - MainMemory sample server.
 *
 * Copyright (C) 2016  Aleksey Demakov
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

#include "common.h"
#include "base/args.h"
#include "base/base.h"
#include "base/daemon.h"
#include "base/exit.h"
#include "base/memory/global.h"
#include "base/settings.h"
#include "base/stdcall.h"
#include "core/core.h"
#include "net/net.h"

#include <fcntl.h>
#include <sys/stat.h>

static void writer(struct mm_net_socket *sock);

// Server descriptor.
static struct mm_net_proto proto = {
	.flags = MM_NET_OUTBOUND,
	.writer = writer,
};

// Server instance.
static struct mm_net_server *server;

// Server response message.
static const char *message;
static size_t message_len;

// Command line arguments table.
static const struct mm_args_info args_tbl[] = {
	{ "help", 'h', MM_ARGS_SPECIAL,
	  "\n\t\tdisplay this help text and exit" },
	{ NULL, 0, 0, NULL },
	{ "port", 'p', MM_ARGS_REQUIRED,
	  "\n\t\thello server TCP port" },
	{ "message", 'm', MM_ARGS_REQUIRED,
	  "\n\t\thello server message ('Hello, World!' by default)" },
	{ "message-file", 'f', MM_ARGS_REQUIRED,
	   "\n\t\tget hello server message from the specified file" },
	{ "daemon", 'd', MM_ARGS_TRIVIAL,
	  "\n\t\trun as a daemon (false by default)" },
};

// Command line arguments table size.
static const size_t args_cnt = sizeof(args_tbl) / sizeof(args_tbl[0]);

static void
read_message(const char *file)
{
	int fd = open(file, O_RDONLY);
	if (fd < 0)
		mm_fatal(errno, "open()");

	struct stat st;
	if (fstat(fd, &st) < 0)
		mm_fatal(errno, "fstat()");

	message_len = st.st_size;
	char *buffer = mm_global_alloc(message_len);
	message = buffer;

	if (mm_read(fd, buffer, message_len) != (ssize_t) message_len)
		mm_fatal(errno, "read()");
	mm_close(fd);
}

int
main(int argc, char *argv[])
{
	// Handle command line arguments.
	mm_settings_init();
	mm_args_init(argc, argv, args_cnt, args_tbl);
	if (mm_args_getargc() > 0) {
		mm_args_usage(args_cnt, args_tbl);
		mm_exit(MM_EXIT_USAGE);
	}
	if (mm_settings_get("help", NULL)) {
		mm_args_usage(args_cnt, args_tbl);
		mm_exit(MM_EXIT_SUCCESS);
	}

	// Get the port number.
	uint32_t port = mm_settings_get_uint32("port", "0");
	if (port == 0 || port > UINT16_MAX)
		mm_fatal(0, "no valid port number is specified");

	// Get the server response message.
	const char *file =  mm_settings_get("message-file", NULL);
	if (file != NULL) {
		if (mm_settings_get("message", NULL) != NULL)
			mm_fatal(0, "the options message and message-file"
				    " are mutually exclusive");
		read_message(file);
	} else {
		message = mm_settings_get("message", "Hello, World!");
		message_len = strlen(message);
	}

	// Initialize subsystems.
	mm_base_init();
	mm_core_init();

	// Create the server.
	server = mm_net_create_inet_server("hello", &proto, "0.0.0.0", port);
	mm_core_register_server(server);

	// Assign event loop to the first core.
	struct mm_bitset event_loop_cores;
	mm_bitset_prepare(&event_loop_cores, &mm_global_arena, 4);
	mm_bitset_set(&event_loop_cores, 0);
	mm_core_set_event_affinity(&event_loop_cores);

	// Daemonize if needed.
	if (mm_settings_get("daemon", NULL) != NULL) {
		mm_daemon_start();
		mm_daemon_stdio(NULL, "hello_server.log");
		mm_daemon_notify();
	}

	// Execute the main loop.
	mm_core_start();

	// Terminate subsystems.
	mm_core_term();
	mm_base_term();
	mm_settings_term();

	return MM_EXIT_SUCCESS;
}

static void
writer(struct mm_net_socket *sock)
{
	const char *msg = message;
	size_t len = message_len;
	while (len) {
		ssize_t n = mm_net_write(sock, msg, len);
		if (n <= 0)
			break;
		msg += n;
		len -= n;
	}
	mm_net_close(sock);
}
