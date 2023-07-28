/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014-2021 Satlab A/S <satlab@satlab.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _SLASH_H_
#define _SLASH_H_

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef SLASH_HAVE_TERMIOS_H
#include <termios.h>
#endif

/* Configuration */
#define SLASH_SHOW_MAX		25	/* Maximum number of commands to list */
#define SLASH_ARG_MAX		16	/* Maximum number of arguments, including command name */

/* Helper macros */
#define slash_max(a,b) \
	({ __typeof__ (a) _a = (a); \
	__typeof__ (b) _b = (b); \
	_a > _b ? _a : _b; })

/* Command flags */
#define SLASH_FLAG_HIDDEN	(1 << 0) /* Hidden and not shown in help or completion */
#define SLASH_FLAG_PRIVILEGED	(1 << 1) /* Privileged and hidden until enabled with slash_set_privileged() */

#define __slash_command(_ident, _group, _name, _func, _args, _help, _flags, _context) \
	__attribute__((section(".slash." # _ident), used, aligned(sizeof(long)))) \
	struct slash_command _ident = {					\
		.name  = #_name,					\
		.parent = _group,					\
		.func  = _func,						\
		.args  = _args,						\
		.help  = _help,						\
		.flags = _flags,					\
		.context = _context,					\
	};

/* Top-level commands */
#define slash_command_ex(_name, _func, _args, _help, _flags, _context)	\
	__slash_command(slash_cmd_ ## _name,				\
			NULL, 						\
			_name, _func, _args, _help, _flags, _context)

#define slash_command(_name, _func, _args, _help) \
	slash_command_ex(_name, _func, _args, _help, 0, NULL)

/* Subcommand */
#define slash_command_sub_ex(_group, _name, _func, _args, _help, _flags, _context) \
	__slash_command(slash_cmd_ ## _group ## _ ## _name,		\
			&(slash_cmd_ ## _group),			\
			_name, _func, _args, _help, _flags, _context)

#define slash_command_sub(_group, _name, _func, _args, _help) 		\
	slash_command_sub_ex(_group, _name, _func, _args, _help, 0, NULL)

/* Subsubcommand */
#define slash_command_subsub_ex(_group, _subgroup, _name, _func, _args, _help, _flags, _context) \
	__slash_command(slash_cmd_ ## _group ## _ ## _subgroup ## _ ## _name,\
			&(slash_cmd_ ## _group ## _ ## _subgroup),	\
			_name, _func, _args, _help, _flags, _context)

#define slash_command_subsub(_group, _subgroup, _name, _func, _args, _help) \
	slash_command_subsub_ex(_group, _subgroup, _name, _func, _args, _help, 0, NULL)

/* Top-level group */
#define slash_command_group_ex(_name, _help, _flags) \
	slash_command_ex(_name, NULL, NULL, _help, _flags, NULL)

#define slash_command_group(_name, _help) \
	slash_command_group_ex(_name, _help, 0)

/* Subgroup */
#define slash_command_subgroup_ex(_group, _name, _help, _flags) \
	slash_command_sub_ex(_group, _name, NULL, NULL, _help, _flags, NULL)

#define slash_command_subgroup(_group, _name, _help) \
	slash_command_sub(_group, _name, NULL, NULL, _help)

/* Command prototype */
struct slash;
typedef int (*slash_func_t)(struct slash *slash);

/* Wait function prototype */
typedef int (*slash_waitfunc_t)(struct slash *slash, unsigned int ms);

/* Command return values */
#define SLASH_EXIT	( 1)
#define SLASH_SUCCESS	( 0)
#define SLASH_EUSAGE	(-1)
#define SLASH_EINVAL	(-2)
#define SLASH_ENOSPC	(-3)
#define SLASH_EIO	(-4)
#define SLASH_ENOMEM	(-5)
#define SLASH_ENOENT	(-6)
#define SLASH_EHELP	(-7)

/* Command struct */
struct slash_command {
	/* Static data */
	const char *name;
	const slash_func_t func;
	const char *args;
	const char *help;

	/* Flags */
	unsigned int flags;

	/* Command context */
	void *context;

	/* Parent command */
	struct slash_command *parent;
};

/* Slash context */
struct slash {
	/* Terminal handling */
#ifdef SLASH_HAVE_TERMIOS_H
	struct termios original;
#endif
	FILE *file_write;
	FILE *file_read;
	slash_waitfunc_t waitfunc;
	bool use_activate;
	bool privileged;
	bool exit_inhibit;

	/* Line editing */
	size_t line_size;
	const char *prompt;
	size_t prompt_length;
	size_t prompt_print_length;
	char *buffer;
	size_t cursor;
	size_t length;
	char last_char;

	/* History */
	size_t history_size;
	int history_depth;
	size_t history_avail;
	int history_rewind_length;
	char *history;
	char *history_head;
	char *history_tail;
	char *history_cursor;

	/* Command interface (1 arg required for final NULL value) */
	char *argv[SLASH_ARG_MAX + 1];
	int argc;
	void *context;

	/* getopt state */
	char *optarg;
	int optind;
	int opterr;
	int optopt;
	int sp;
};

struct slash *slash_create(size_t line_size, size_t history_size);

void slash_destroy(struct slash *slash);

int slash_init(struct slash *slash,
	       char *line, size_t line_size,
	       char *history, size_t history_size);

int slash_refresh(struct slash *slash);

void slash_reset(struct slash *slash);

char *slash_readline(struct slash *slash, const char *prompt);

int slash_execute(struct slash *slash, char *line);

int slash_loop(struct slash *slash, const char *prompt_good, const char *prompt_bad);

int slash_wait_interruptible(struct slash *slash, unsigned int ms);

int slash_set_wait_interruptible(struct slash *slash, slash_waitfunc_t waitfunc);

int slash_printf(struct slash *slash, const char *format, ...);

int slash_getopt(struct slash *slash, const char *optstring);

void slash_clear_screen(struct slash *slash);

void slash_require_activation(struct slash *slash, bool activate);

void slash_inhibit_exit(struct slash *slash, bool inhibit);

void slash_set_privileged(struct slash *slash, bool privileged);

#endif /* _SLASH_H_ */
