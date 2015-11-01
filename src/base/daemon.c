/*
 * base/daemon.c - Daemonize routine.
 *
 * Copyright (C) 2015  Aleksey Demakov
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

#include "base/daemon.h"

#include "base/log/debug.h"
#include "base/log/error.h"
#include "base/log/log.h"
#include "base/util/exit.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

// Daemon start notification pipe.
static int mm_daemon_pipe[2];

void
mm_daemon_start(void)
{
	// Open the notification pipe.
	if (pipe(mm_daemon_pipe) < 0)
		mm_fatal(errno, "pipe()");

	// Fork a child process that is guaranteed not to be a process
	// group leader.
	pid_t pid = fork();
	if (pid < 0)
		mm_fatal(errno, "fork()");

	// The parent process exits after waiting for a notification
	// from the daemon.
	if (pid > 0) {
		// Close the pipe write end.
		close(mm_daemon_pipe[1]);

		char status;
		for (;;) {
			ssize_t n = read(mm_daemon_pipe[0], &status, sizeof status);
			if (n > 0)
				break;
			if (n == 0 || errno != EINTR) {
				status = EXIT_FAILURE;
				break;
			}
		}

		// Close the pipe read end.
		close(mm_daemon_pipe[0]);

		if (status != EXIT_SUCCESS) {
			mm_error(0, "failed to spawn a daemon process.");
			mm_exit(status);
		}

		exit(EXIT_SUCCESS);
	}

	// Close the pipe read end.
	close(mm_daemon_pipe[0]);

	// Become a process group and session group leader thus detaching
	// from a controlling terminal.
	if (setsid() < 0)
		mm_fatal(errno, "setsid()");

	// Fork another child process that is guaranteed to never acquire
	// a new controlling terminal.
	pid = fork();
	if (pid < 0)
		mm_fatal(errno, "fork()");
	if (pid > 0)
		exit(EXIT_FAILURE);
}

void
mm_daemon_stdio(const char *input, const char *output)
{
	// Redirect standard input to /dev/null.
	close(STDIN_FILENO);
	if (input == NULL)
		input = "/dev/null";
	int fd = open(input, O_RDONLY);
	if (fd != STDIN_FILENO)
		mm_fatal(errno, "open(\"%s\"), ...", input);

	// Redirect standard output and error.
	int oflags = O_WRONLY | O_CREAT | O_APPEND;
	if (output == NULL) {
		output = "/dev/null";
		oflags = O_WRONLY;
	}
	fd = open(output, oflags, 0644);
	if (fd < 0)
		mm_fatal(errno, "open(\"%s\"), ...", output);
	if (dup2(fd, STDOUT_FILENO) < 0)
		mm_fatal(errno, "dup2()");
	if (dup2(fd, STDERR_FILENO) < 0)
		mm_fatal(errno, "dup2()");
	if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
		close(fd);
}

void
mm_daemon_notify(void)
{
	ASSERT(mm_daemon_pipe[1] >= 0);

	// Send the success notification.
	char status = EXIT_SUCCESS;
	for (;;) {
		ssize_t n = write(mm_daemon_pipe[1], &status, sizeof status);
		if (n > 0)
			break;
		if (n < 0 && errno != EINTR)
			mm_fatal(errno, "write()");
	}

	// Close the pipe write end.
	close(mm_daemon_pipe[1]);
}
