/*
 * main.c - MainMemory main routine.
 *
 * Copyright (C) 2012-2017  Aleksey Demakov
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

#include "base/bitset.h"
#include "base/exit.h"
#include "base/conf.h"
#include "base/report.h"
#include "base/runtime.h"
#include "base/settings.h"
#include "base/memory/global.h"

#include "net/net.h"

#include "memcache/memcache.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

static void
mm_term_handler(int signo UNUSED)
{
	ENTER();

	mm_stop();

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
mm_server_init(void)
{
	ENTER();

	const char *addr = mm_settings_get("memcache-ip", "127.0.0.1");
	uint32_t port = mm_settings_get_uint32("memcache-port", 11211);
	uint32_t mbytes = mm_settings_get_uint32("memcache-memory", 64);
	uint32_t nparts = mm_settings_get_uint32("memcache-partitions", 8);

	struct mm_memcache_config memcache_config;
	memcache_config.addr = addr;
	memcache_config.port = port;
	memcache_config.volume = mbytes * 1024 * 1024;
	memcache_config.nparts = nparts;
#if ENABLE_MEMCACHE_DELEGATE
	mm_bitset_prepare(&memcache_config.affinity, &mm_common_space.arena, 8);
	mm_bitset_set(&memcache_config.affinity, 6);
	mm_bitset_set(&memcache_config.affinity, 7);
#endif
	mm_memcache_init(&memcache_config);

	LEAVE();
}

static struct mm_args_info mm_args_info_tbl[] = {
	{ "help", 'h', MM_ARGS_SPECIAL,
	  "\n\t\tdisplay this help text and exit" },
	{ "version", 'V', MM_ARGS_SPECIAL,
	   "\n\t\tdisplay version information and exit" },
	{ "verbose", 'v', MM_ARGS_TRIVIAL,
	  "\n\t\tenable verbose messages" },
	{ "warning", 'w', MM_ARGS_TRIVIAL,
	  "\n\t\tenable warning messages" },
	{ NULL, 0, 0, NULL },
	{ "config", 'c', MM_ARGS_REQUIRED,
	  "\n\t\tconfiguration file" },
	{ "daemon", 'd', MM_ARGS_TRIVIAL,
	  "\n\t\trun as a daemon" },
	{ "thread-affinity", 0, MM_ARGS_REQUIRED,
	  "\n\t\tenable thread binding to CPU cores" },
	{ "thread-number", 't', MM_ARGS_REQUIRED,
	  "\n\t\tnumber of threads" },
	{ NULL, 0, 0, NULL },
	{ "memcache-ip", 'l', MM_ARGS_REQUIRED,
	  "\n\t\tmemcache server IP address to listen on" },
	{ "memcache-port", 'p', MM_ARGS_REQUIRED,
	  "\n\t\tmemcache server TCP port" },
	{ "memcache-memory", 'm', MM_ARGS_REQUIRED,
	  "\n\t\tmemory for memcache items in megabytes" },
	{ "memcache-partitions", 'M', MM_ARGS_REQUIRED,
	  "\n\t\tnumber of memcache table partitions" },
};

static size_t mm_args_info_cnt = sizeof(mm_args_info_tbl) / sizeof(mm_args_info_tbl[0]);

int
main(int argc, char *argv[])
{
	ENTER();

	// The very basic setup.
	mm_init(argc, argv, mm_args_info_cnt, mm_args_info_tbl);

	// Handle command line arguments.
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

	// Load the configuration file.
	mm_conf_load(mm_settings_get("config-file", NULL));
	mm_set_verbose_enabled(mm_settings_get("verbose", NULL) != NULL);
	mm_set_warning_enabled(mm_settings_get("warning", NULL) != NULL);
	if (mm_settings_get("daemon", NULL) != NULL)
		mm_set_daemon_mode("mmd.log");

	// Set signal handlers.
	mm_signal_init();

	// Initialize servers.
	mm_server_init();

	// Execute the main loop.
	mm_start();

	LEAVE();
	return MM_EXIT_SUCCESS;
}
