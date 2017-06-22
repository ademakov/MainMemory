/*
 * proxy_server.c - MainMemory sample proxy server.
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
#include "base/daemon.h"
#include "base/exit.h"
#include "base/init.h"
#include "base/report.h"
#include "base/runtime.h"
#include "base/settings.h"
#include "base/stdcall.h"
#include "base/fiber/core.h"
#include "base/memory/global.h"
#include "base/memory/memory.h"
#include "net/netbuf.h"

#include <stdio.h>

static struct mm_net_socket * proxy_create(void);
static void proxy_destroy(struct mm_net_socket *sock);
static void proxy_reader(struct mm_net_socket *sock);

struct proxy_command
{
	struct mm_link link;
	struct mm_net_addr addr;

	char *reply_msg;
	size_t reply_len;
};

struct client_conn
{
	struct mm_netbuf_socket sock;
	struct mm_list commands;
};

// Server descriptor.
static struct mm_net_proto proxy_proto = {
	.flags = MM_NET_INBOUND,
	.create = proxy_create,
	.destroy = proxy_destroy,
	.reader = proxy_reader,
};

// Server instance.
static struct mm_net_server *proxy_server;

// Command line arguments table.
static const struct mm_args_info args_tbl[] = {
	{ NULL, 0, 0, "<port>" },
	{ "help", 'h', MM_ARGS_SPECIAL,
	  "\n\t\tdisplay this help text and exit" },
	{ "daemon", 'd', MM_ARGS_TRIVIAL,
	  "\n\t\trun as a daemon (false by default)" },
};

// Command line arguments table size.
static const size_t args_cnt = sizeof(args_tbl) / sizeof(args_tbl[0]);

int
main(int ac, char *av[])
{
	// Parse command line arguments.
	mm_init(ac, av, args_cnt, args_tbl);
	ac = mm_args_getargc();
	av = mm_args_getargv();

	// Handle the help option.
	if (mm_settings_get("help", NULL)) {
		mm_args_usage(args_cnt, args_tbl);
		mm_exit(MM_EXIT_SUCCESS);
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

	// Initialize subsystems.
	mm_base_init();

	// Create the server.
	proxy_server = mm_net_create_inet_server("hello", &proxy_proto,
						 "0.0.0.0", port);
	mm_net_setup_server(proxy_server);

	// Daemonize if needed.
	if (mm_settings_get("daemon", NULL) != NULL) {
		mm_daemon_start();
		mm_daemon_stdio(NULL, "proxy_seerver.log");
		mm_daemon_notify();
	}

	// Execute the main loop.
	mm_base_loop();

	// Terminate subsystems.
	mm_base_term();

	return MM_EXIT_SUCCESS;
}

// Create a command.
static struct proxy_command *
command_create(void)
{
	struct proxy_command *command = mm_regular_alloc(sizeof(struct proxy_command));
	memset(command, 0, sizeof *command);
	return command;
}

// Destroy a command.
static void
command_destroy(struct proxy_command *command)
{
	if (command->reply_msg != NULL)
		mm_regular_free(command->reply_msg);
	mm_regular_free(command);
}

// Create a socket that serves an incoming connection.
static struct mm_net_socket *
proxy_create(void)
{
	struct client_conn *client = mm_regular_alloc(sizeof(struct client_conn));
	mm_netbuf_prepare(&client->sock);
	mm_list_prepare(&client->commands);
	return &client->sock.sock;
}

// Destroy a socket that served an incoming connection.
static void
proxy_destroy(struct mm_net_socket *sock)
{
	struct client_conn *client = containerof(sock, struct client_conn, sock.sock);
	while (!mm_list_empty(&client->commands)) {
		struct mm_link *link = mm_list_remove_head(&client->commands);
		struct proxy_command *command = containerof(link, struct proxy_command, link);
		command_destroy(command);
	}
	mm_netbuf_cleanup(&client->sock);
	mm_regular_free(client);
}

// Parse a command -- get the target IP address and port.
static struct proxy_command *
proxy_parse(char *addr, char *end)
{
	if (*(end - 1) != '\r')
		return NULL;

	char *sep = memchr(addr, ':', end - addr - 1);
	if (sep == NULL)
		return NULL;

	size_t host_len = sep - addr;
	size_t port_len = end - sep - 2;
	if (host_len == 0 || host_len > 15 || port_len == 0 || port_len > 5)
		return false;

	char *host_str = addr;
	char *port_str = sep + 1;
	host_str[host_len] = 0;
	port_str[port_len] = 0;

	uint32_t port = atoi(port_str);
	if (port == 0 || port > UINT16_MAX)
		return NULL;

	struct mm_net_addr net_addr;
	if (!mm_net_set_inet_addr(&net_addr, host_str, port))
		return NULL;

	struct proxy_command *command = command_create();
	command->addr.in_addr = net_addr.in_addr;
	return command;
}

// Read response message from the target server.
static void
proxy_read(struct proxy_command *command)
{
	struct mm_net_socket *sock = mm_net_create();
	if (mm_net_connect(sock, &command->addr) < 0) {
		mm_error(errno, "Connect failure");
		mm_net_destroy(sock);
		return;
	}

	size_t reply_len = 0, reply_max = 1024;
	char *reply_msg = mm_regular_alloc(reply_max);
	for (;;) {
		if ((reply_max - reply_len) < 512) {
			reply_max *= 2;
			reply_msg = mm_private_realloc(reply_msg, reply_max);
		}

		char *ptr = reply_msg + reply_len;
		size_t len = reply_max - reply_len;
		ssize_t n = mm_net_read(sock, ptr, len);
		if (n <= 0) {
			if (n < 0) {
				mm_error(errno, "Read failure");
				mm_regular_free(reply_msg);
				reply_msg = NULL;
				reply_len = 0;
			}
			break;
		}

		reply_len += n;
	}

	command->reply_msg = reply_msg;
	command->reply_len = reply_len;

	mm_net_close(sock);
}

// Send response message to the client.
static void
proxy_write(struct client_conn *client, struct proxy_command *command)
{
	struct mm_net_socket *sock = &client->sock.sock;
	const char *msg = command->reply_msg;
	size_t len = command->reply_len;
	while (len) {
		ssize_t n = mm_net_write(sock, msg, len);
		if (n <= 0)
			break;
		msg += n;
		len -= n;
	}
}

// Execute a single command.
static void
proxy_handle(struct client_conn *client, struct proxy_command *command)
{
	proxy_read(command);
	proxy_write(client, command);
	command_destroy(command);

	//mm_list_append(&client->commands, &command->link);
	//mm_list_delete(&command->link);
}

// Read and execute incoming commands from a client.
static void
proxy_reader(struct mm_net_socket *sock)
{
	struct client_conn *client = containerof(sock, struct client_conn, sock.sock);
	for (mm_timeout_t timeout = 0; ; timeout = 10000) {
		mm_net_set_read_timeout(&client->sock.sock, timeout);
		ssize_t rc = mm_netbuf_fill(&client->sock, 1);
		if (rc <= 0) {
			if (rc == 0 || (errno != ETIMEDOUT && errno != EAGAIN))
				mm_netbuf_close(&client->sock);
			break;
		}

		for (;;) {
			// Seek the command terminator char in the read buffer.
			size_t off = 0;
			char *e = mm_netbuf_find(&client->sock, '\n', &off);
			if (e == NULL) {
				if (off > 32) {
					mm_netbuf_close(&client->sock);
					mm_error(0, "Missing command terminator");
				}
				break;
			}

			// Parse and handle the command.
			char *p = mm_netbuf_rget(&client->sock);
			struct proxy_command *command = proxy_parse(p, e);
			if (command == NULL) {
				mm_netbuf_close(&client->sock);
				mm_error(0, "Invalid command");
				break;
			}
			proxy_handle(client, command);

			// Advance the read buffer position.
			mm_netbuf_radd(&client->sock, off + 1);
		}

		// Consume already parsed read buffer data.
		mm_netbuf_read_reset(&client->sock);
	}
}
