/*
 * base/topology.c - MainMemory hardware topology.
 *
 * Copyright (C) 2013-2015  Aleksey Demakov
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

#include "base/topology.h"
#include "base/log/error.h"

#include <unistd.h>

#ifdef HAVE_SYS_SYSCTL_H
# include <sys/sysctl.h>
#endif

#define MM_DEFAULT_NCPUS	1

int
mm_topology_getncpus(void)
{
#if ENABLE_SMP
# if defined(HAVE_SYS_SYSCTL_H) && defined(HW_AVAILCPU)
//#  define SELECTOR "hw.ncpu"
#  define SELECTOR "hw.activecpu"
//#  define SELECTOR "hw.physicalcpu"
	int num;
	size_t len = sizeof num;
	if (sysctlbyname(SELECTOR, &num, &len, NULL, 0) < 0)
		mm_fatal(errno, "Failed to count cores.");
	return num;
# elif defined(_SC_NPROCESSORS_ONLN)
	int nproc_onln = sysconf(_SC_NPROCESSORS_ONLN);
	if (nproc_onln < 0)
		mm_fatal(errno, "Failed to count cores.");
	return nproc_onln;
# else
#  error "Unsupported SMP architecture."
# endif
#endif
	return MM_DEFAULT_NCPUS;
}
