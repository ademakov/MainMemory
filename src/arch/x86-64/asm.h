/*
 * asm.h - MainMemory asm macros.
 *
 * Copyright (C) 2012  Aleksey Demakov
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

#ifndef ASM_H
#define ASM_H

/* Define the ASM name for a C function. */
#ifdef __APPLE__
# define C_NAME(name) _##name
#else
# define C_NAME(name) name
#endif

/* The ASM directives that start and end a function. */
#ifdef __APPLE__
# define PROC_ENTRY(name) .align 4,0x90; .globl name; name:
# define PROC_START
# define PROC_END
#else
# define PROC_ENTRY(name) .align 16,0x90; .globl name; .type x,@function; name:
# define PROC_START .cfi_startproc
# define PROC_END .cfi_endproc
#endif

#endif /* ASM_H */
