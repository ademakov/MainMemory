/*
 * main.c - MainMemory main routine.
 *
 * Copyright (C) 2012-2014  Aleksey Demakov
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

#include "core/core.h"

#include "base/bitset.h"
#include "base/daemon.h"
#include "base/json.h"
#include "base/event/event.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/plain.h"
#include "base/log/trace.h"
#include "base/memory/alloc.h"
#include "base/util/exit.h"

#include "net/net.h"

#include "memcache/memcache.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static struct mm_net_server *mm_ucmd_server;
static struct mm_net_server *mm_icmd_server;

static bool mm_daemonize = false;

static void
mm_term_handler(int signo __mm_unused__)
{
	ENTER();

	mm_core_stop();
	mm_exit_set();

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
mm_cmd_reader(struct mm_net_socket *sock)
{
	ENTER();

	char buf[1024];
	int n = mm_net_read(sock, buf, sizeof(buf));
	if (n <= 0) {
		if (n < 0)
			mm_error(errno, "read()");
		mm_net_close(sock);
	}

	mm_net_write(sock, "test\n", 5);
	mm_net_close(sock);

	LEAVE();
}

static void
mm_server_init(void)
{
	ENTER();

	// Assign event loops to first three cores.
	struct mm_bitset event_loop_cores;
	mm_bitset_prepare(&event_loop_cores, &mm_global_arena, 4);
	mm_bitset_set(&event_loop_cores, 0);
	mm_bitset_set(&event_loop_cores, 1);
	mm_bitset_set(&event_loop_cores, 2);
	mm_bitset_set(&event_loop_cores, 3);
	mm_core_set_event_affinity(&event_loop_cores);

	static struct mm_net_proto proto = {
		.flags = MM_NET_INBOUND,
		.prepare = NULL,
		.cleanup = NULL,
		.reader = mm_cmd_reader,
		.writer = NULL,
	};

	mm_ucmd_server = mm_net_create_unix_server("test", &proto,
						   "mm_cmd.sock");
	mm_icmd_server = mm_net_create_inet_server("test", &proto,
						   "127.0.0.1", 8000);

	//mm_core_register_server(mm_ucmd_server);
	mm_core_register_server(mm_icmd_server);

	struct mm_memcache_config memcache_config;
	memcache_config.volume = 64 * 1024 * 1024;
#if ENABLE_MEMCACHE_DELEGATE
	mm_bitset_prepare(&memcache_config.affinity, &mm_common_space.arena, 8);
	mm_bitset_set(&memcache_config.affinity, 6);
	mm_bitset_set(&memcache_config.affinity, 7);
#else
	memcache_config.nparts = 1;
#endif
	mm_memcache_init(&memcache_config);

	LEAVE();
}

static void
mm_cfg_read(int fd, const char *name, struct mm_json_reader *reader)
{
	static char buffer[1024];
	ssize_t n = read(fd, buffer, sizeof buffer);
	if (n < 0)
		mm_fatal(errno, "configuration file: %s", name);
	if (n == 0)
		mm_fatal(0, "configuration file: %s: invalid data", name);
	mm_json_reader_feed(reader, buffer, n);
}

static mm_json_token_t
mm_cfg_next(int fd, const char *name, struct mm_json_reader *reader)
{
	for (;;) {
		mm_json_token_t token = mm_json_reader_next(reader);
		if (token == MM_JSON_INVALID)
			mm_fatal(0, "configuration file: %s: invalid data", name);
		if (token != MM_JSON_PARTIAL && token != MM_JSON_START_DOCUMENT)
			return token;
		mm_cfg_read(fd, name, reader);
	}
}

static void
mm_cfg_skip(int fd, const char *name, struct mm_json_reader *reader)
{
	for (;;) {
		mm_json_token_t token = mm_json_reader_skip(reader);
		if (token == MM_JSON_INVALID)
			mm_fatal(0, "configuration file: %s: invalid data", name);
		if (token != MM_JSON_PARTIAL)
			return;
		mm_cfg_read(fd, name, reader);
	}
}

static void
mm_cfg_load(const char *name)
{
	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		mm_error(errno, "configuration file: %s", name);
		return;
	}

	struct mm_json_reader reader;
	mm_json_reader_prepare(&reader, &mm_global_arena);
	mm_json_token_t token = mm_cfg_next(fd, name, &reader);
	if (token != MM_JSON_START_OBJECT)
		mm_fatal(0, "configuration file: %s: invalid data", name);

	do {
		token = mm_cfg_next(fd, name, &reader);
		if (token == MM_JSON_END_OBJECT)
			break;

		if (mm_json_reader_string_equals(&reader, "daemon")) {
			token = mm_cfg_next(fd, name, &reader);
			mm_daemonize = (token == MM_JSON_TRUE);
		} else if (mm_json_reader_string_equals(&reader, "verbose")) {
			token = mm_cfg_next(fd, name, &reader);
			mm_enable_verbose(token == MM_JSON_TRUE);
		} else  if (mm_json_reader_string_equals(&reader, "warning")) {
			token = mm_cfg_next(fd, name, &reader);
			mm_enable_warning(token == MM_JSON_TRUE);
		} else {
			mm_cfg_skip(fd, name, &reader);
		}

	} while (token != MM_JSON_END_OBJECT);

	mm_json_reader_cleanup(&reader);
	close(fd);
}

int
main(int ac, char *av[])
{
	ENTER();

	// Set configuration options.
	const char *cfg_file_name = "mmem.json";
	if (ac > 1)
		cfg_file_name = av[1];
	mm_cfg_load(cfg_file_name);

	// Initialize subsystems.
	mm_core_init();

	// Daemonize if needed.
	if (mm_daemonize) {
		mm_daemon_start();
		mm_daemon_stdio(NULL, "mmem.log");
		mm_daemon_notify();
	}

	// Set signal handlers.
	mm_signal_init();

	// Initialize servers.
	mm_server_init();

	// Execute the main loop.
	mm_core_start();

	// Terminate subsystems.
	mm_core_term();

	LEAVE();
	mm_log_relay();
	mm_log_flush();
	return EXIT_SUCCESS;
}
