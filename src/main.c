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

#include "base/args.h"
#include "base/base.h"
#include "base/bitset.h"
#include "base/exit.h"
#include "base/conf.h"
#include "base/daemon.h"
#include "base/settings.h"
#include "base/event/event.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/log/plain.h"
#include "base/log/trace.h"
#include "base/memory/global.h"

#include "net/net.h"

#include "memcache/memcache.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static struct mm_net_server *mm_ucmd_server;
static struct mm_net_server *mm_icmd_server;

static bool mm_daemonize = false;

static void
mm_term_handler(int signo UNUSED)
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
		mm_fatal(errno, "failed sigaction() call");
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

static struct mm_args_info mm_args_info_tbl[] = {
	{ "help", 'h', MM_ARGS_SPECIAL, "\n\t\tdisplay this help text and exit" },
	{ "version", 'V', MM_ARGS_SPECIAL, "\n\t\tdisplay version information and exit" },
	{ "verbose", 'v', MM_ARGS_TRIVIAL, "\n\t\tenable verbose messages" },
	{ "warning", 'w', MM_ARGS_TRIVIAL, "\n\t\tenable warning messages" },
	{ NULL, 0, 0, NULL },
	{ "config", 'c', MM_ARGS_REQUIRED, "\n\t\tconfiguration file" },
	{ "daemon", 'd', MM_ARGS_TRIVIAL, "\n\t\trun as a daemon" },
	{ "thread-number", 't', MM_ARGS_REQUIRED, "\n\t\tnumber of threads" },
};

static size_t mm_args_info_cnt = sizeof(mm_args_info_tbl) / sizeof(mm_args_info_tbl[0]);

int
main(int argc, char *argv[])
{
	ENTER();

	mm_settings_init();

	// Handle command line arguments.
	mm_args_init(argc, argv, mm_args_info_cnt, mm_args_info_tbl);
	if (mm_args_getargc() > 0) {
		mm_args_usage(mm_args_info_cnt, mm_args_info_tbl);
		mm_exit(MM_EXIT_USAGE);
	}
	if (mm_settings_get("help", NULL)) {
		mm_args_usage(mm_args_info_cnt, mm_args_info_tbl);
		mm_exit(MM_EXIT_SUCCESS);
	}
	if (mm_settings_get("version", NULL)) {
		mm_brief("%s", PACKAGE_STRING);
		mm_exit(MM_EXIT_SUCCESS);
	}

	// Load configuration file.
	mm_conf_load(mm_settings_get("config", NULL));
	mm_set_verbose_enabled(mm_settings_get("verbose", NULL) != NULL);
	mm_set_warning_enabled(mm_settings_get("warning", NULL) != NULL);

	// Initialize subsystems.
	mm_base_init();
	mm_core_init();

	// Daemonize if needed.
	if (mm_settings_get("daemon", NULL) != NULL) {
		mm_daemonize = true;
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
	mm_base_term();
	mm_settings_term();

	LEAVE();
	mm_log_relay();
	mm_log_flush();
	return MM_EXIT_SUCCESS;
}
