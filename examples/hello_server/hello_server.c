/*
 * hello_server.c - MainMemory sample server.
 *
 * Copyright (C) 2016-2017  Aleksey Demakov
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
#include "base/runtime.h"
#include "base/settings.h"
#include "base/stdcall.h"
#include "base/memory/alloc.h"
#include "base/net/net.h"

#include <fcntl.h>
#include <sys/stat.h>

static mm_value_t hello_writer(mm_value_t arg);

// Server descriptor.
static struct mm_net_proto hello_proto = { .writer = hello_writer };

// Server instance.
static struct mm_net_server *hello_server;

// Server response message.
static const char *hello_msg;
static size_t hello_len;

// Command line arguments table.
static const struct mm_args_info args_tbl[] = {
	{ NULL, 0, 0, "<port>" },
	{ "help", 'h', MM_ARGS_COMMAND,
	  "\n\t\tdisplay this help text and exit" },
	{ "daemon", 'd', MM_ARGS_TRIVIAL,
	   "\n\t\trun as a daemon (false by default)" },
	{ "message", 'm', MM_ARGS_REQUIRED,
	  "\n\t\thello server message ('Hello, World!' by default)" },
	{ "message-file", 'f', MM_ARGS_REQUIRED,
	   "\n\t\tget hello server message from the specified file" },
};

// Command line arguments table size.
static const size_t args_cnt = sizeof(args_tbl) / sizeof(args_tbl[0]);

// This function loads the response message from a file.
static void
read_hello_message(const char *file)
{
	int fd = open(file, O_RDONLY);
	if (fd < 0)
		mm_fatal(errno, "open()");

	struct stat st;
	if (fstat(fd, &st) < 0)
		mm_fatal(errno, "fstat()");

	hello_len = st.st_size;
	char *buffer = mm_memory_xalloc(hello_len);
	hello_msg = buffer;

	if (mm_read(fd, buffer, hello_len) != (ssize_t) hello_len)
		mm_fatal(errno, "read()");
	mm_close(fd);
}

int
main(int ac, char *av[])
{
	// Parse command line arguments.
	mm_init(ac, av, args_cnt, args_tbl);
	ac = mm_args_argc();
	av = mm_args_argv();

	// Handle the help option.
	if (mm_settings_get("help", NULL)) {
		mm_args_usage(args_cnt, args_tbl);
		return EXIT_SUCCESS;
	}

	// Parse the required port number parameter.
	if (ac != 1) {
		mm_args_usage(args_cnt, args_tbl);
		mm_fatal(0, "\nNo port number is provided.");
	}
	uint32_t port = atoi(av[0]);
	if (port == 0 || port > UINT16_MAX) {
		mm_args_usage(args_cnt, args_tbl);
		mm_fatal(0, "\nInvalid port number is provided.");
	}

	// Get the server response message.
	const char *file = mm_settings_get("message-file", NULL);
	if (file != NULL) {
		if (mm_settings_get("message", NULL) != NULL)
			mm_fatal(0, "the options message and message-file"
				    " are mutually exclusive");
		read_hello_message(file);
	} else {
		hello_msg = mm_settings_get("message", "Hello, World!\n");
		hello_len = strlen(hello_msg);
	}

	// Daemonize if needed.
	if (mm_settings_get("daemon", NULL) != NULL)
		mm_set_daemon_mode("hello_server.log");

	// Create the server.
	hello_server = mm_net_create_inet_server("hello", &hello_proto,
						 "0.0.0.0", port);
	mm_net_setup_server(hello_server);

	// Execute the main loop.
	mm_start();

	return EXIT_SUCCESS;
}

// This function gets an open client connection as argument,
// transmits the response message and closes the connection.
static mm_value_t
hello_writer(mm_value_t arg)
{
	struct mm_net_socket *sock = mm_net_arg_to_socket(arg);

	const char *msg = hello_msg;
	size_t len = hello_len;
	while (len) {
		ssize_t n = mm_net_write(sock, msg, len);
		if (n <= 0)
			break;
		msg += n;
		len -= n;
	}
	mm_net_close(sock);

	return 0;
}
