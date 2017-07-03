/*
 * base/runtime.h - Base library runtime.
 *
 * Copyright (C) 2015-2017  Aleksey Demakov
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

#ifndef BASE_RUNTIME_H
#define BASE_RUNTIME_H

#include "common.h"
#include "base/args.h"

/**********************************************************************
 * Runtime information.
 **********************************************************************/

mm_thread_t
mm_number_of_regular_domains(void);

mm_thread_t
mm_number_of_regular_threads(void);

extern struct mm_domain *mm_regular_domain;
extern struct mm_strand *mm_regular_strands;

/**********************************************************************
 * Runtime start and stop hooks.
 **********************************************************************/

void NONNULL(1)
mm_common_start_hook_0(void (*proc)(void));
void NONNULL(1)
mm_common_start_hook_1(void (*proc)(void *), void *data);

void NONNULL(1)
mm_common_stop_hook_0(void (*proc)(void));
void NONNULL(1)
mm_common_stop_hook_1(void (*proc)(void *), void *data);

void NONNULL(1)
mm_regular_start_hook_0(void (*proc)(void));
void NONNULL(1)
mm_regular_start_hook_1(void (*proc)(void *), void *data);

void NONNULL(1)
mm_regular_stop_hook_0(void (*proc)(void));
void NONNULL(1)
mm_regular_stop_hook_1(void (*proc)(void *), void *data);

void NONNULL(1)
mm_regular_thread_start_hook_0(void (*proc)(void));
void NONNULL(1)
mm_regular_thread_start_hook_1(void (*proc)(void *), void *data);

void NONNULL(1)
mm_regular_thread_stop_hook_0(void (*proc)(void));
void NONNULL(1)
mm_regular_thread_stop_hook_1(void (*proc)(void *), void *data);

/**********************************************************************
 * Runtime control routines.
 **********************************************************************/

void NONNULL(2)
mm_init(int argc, char *argv[], size_t ninfo, const struct mm_args_info *info);

void
mm_set_daemon_mode(const char *log_file);

void
mm_start(void);

void
mm_stop(void);

#endif /* BASE_RUNTIME_H */
