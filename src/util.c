/*
 * error.c - MainMemory errors.
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

#include <util.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

void
mm_vprintf(const char *restrict msg, va_list va)
{
	vfprintf(stderr, msg, va);
	fprintf(stderr, "\n");
}


void
mm_error(const char *restrict msg, ...)
{
	va_list va;
	va_start(va, msg);
	mm_vprintf(msg, va);
	va_end(va);
}

void
mm_fatal(const char *restrict msg, ...)
{
	va_list va;
	va_start(va, msg);
	mm_vprintf(msg, va);
	va_end(va);
	
	exit(EXIT_FAILURE);
}
