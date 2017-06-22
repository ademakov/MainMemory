/*
 * base/init.c - MainMemory initialization.
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

#include "base/init.h"

#include "base/exit.h"
#include "base/report.h"
#include "base/settings.h"

void NONNULL(2)
mm_init(int argc, char *argv[], size_t ninfo, const struct mm_args_info *info)
{
	ENTER();

	// Prepare for graceful exit.
	mm_exit_init();

	// Prepare the settings storage.
	mm_settings_init();

	// Parse the command line arguments.
	mm_args_init(argc, argv, ninfo, info);

	LEAVE();
}

void
mm_term(void)
{
	ENTER();
	LEAVE();
}
