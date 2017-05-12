/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014-2016 Satlab ApS <satlab@satlab.com>
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

#include <stddef.h>
#include <stdbool.h>

#ifdef SLASH_HAVE_TERMIOS_H
#include <termios.h>
#endif

/* Helper macros */
#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (void *)__mptr - offsetof(type,member) );})
#endif

#ifndef stringify
#define stringify(_var) #_var
#endif

#define slash_max(a,b) \
	({ __typeof__ (a) _a = (a); \
	__typeof__ (b) _b = (b); \
	_a > _b ? _a : _b; })

/* List functions */
struct slash_list {
	struct slash_list *prev;
	struct slash_list *next;
};

#define SLASH_LIST_INIT(name) { &(name), &(name) }
#define SLASH_LIST(name) struct slash_list name = SLASH_LIST_INIT(name)

#define slash_list_for_each(pos, head, member)				\
	for (pos = container_of((head)->next, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = container_of(pos->member.next, typeof(*pos), member))

static inline bool slash_list_is_init(struct slash_list *list)
{
	return list->prev && list->next;
}

static inline void slash_list_init(struct slash_list *list)
{
	list->prev = list;
	list->next = list;
}

static inline void slash_list_insert(struct slash_list *list, struct slash_list *elm)
{
	elm->prev = list;
	elm->next = list->next;
	list->next = elm;
	elm->next->prev = elm;
}

static inline void slash_list_insert_tail(struct slash_list *list, struct slash_list *elm)
{
	elm->next = list;
	elm->prev = list->prev;
	list->prev = elm;
	elm->prev->next = elm;
}

static inline int slash_list_empty(struct slash_list *list)
{
	return list->next == list;
}

static inline int slash_list_head(struct slash_list *list,
				  struct slash_list *cur)
{
	return list == cur;
}

#define __slash_command(_ident, _group, _name, _func, _args, _help) 	\
	char _ident ## _str[] = stringify(_name);			\
	__attribute__((section("slash")))				\
	__attribute__((used))						\
	struct slash_command _ident = {					\
		.name  = _ident ## _str,				\
		.group = _group,					\
		.func  = _func,						\
		.args  = _args,						\
		.help  = _help,						\
	};

#define slash_command(_name, _func, _args, _help)			\
	__slash_command(slash_cmd_ ## _name,				\
			NULL, 						\
			_name, _func, _args, _help)

#define slash_command_sub(_group, _name, _func, _args, _help)		\
	__slash_command(slash_cmd_ ## _group ## _ ## _name,		\
			&(slash_cmd_ ## _group),			\
			_name, _func, _args, _help)

#define slash_command_subsub(_group, _subgroup, _name, _func, _args, _help) \
	__slash_command(slash_cmd_ ## _group ## _ ## _subgroup ## _name, \
			&(slash_cmd_ ## _group ## _ ## _subgroup), 	\
			_name, _func, _args, _help)

#define slash_command_group(_name, _help)				\
	slash_command(_name, NULL, NULL, _help)

#define slash_command_subgroup(_group, _name, _help)			\
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

/* Command struct */
struct slash_command {
	/* Static data */
	char *name;
	const slash_func_t func;
	const char *args;
	const char *help;

	/* Parent command */
	struct slash_command *group;

	/* Subcommand list */
	struct slash_list sub;

	/* List member structures */
	struct slash_list command;
	struct slash_list completion;
};

/* Slash context */
struct slash {
	/* Commands */
	struct slash_list commands;

	/* Terminal handling */
#ifdef SLASH_HAVE_TERMIOS_H
	struct termios original;
#endif
	int fd_write;
	int fd_read;
	slash_waitfunc_t waitfunc;

	/* Line editing */
	size_t line_size;
	const char *prompt;
	size_t prompt_length;
	size_t prompt_print_length;
	char *buffer;
	size_t cursor;
	size_t length;
	bool escaped;
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

	/* Command interface */
	char **argv;
	int argc;

	/* getopt state */
	char *optarg;
	int optind;
	int opterr;
	int optopt;
	int sp;
};

struct slash *slash_create(size_t line_size, size_t history_size);

void slash_destroy(struct slash *slash);

char *slash_readline(struct slash *slash, const char *prompt);

int slash_execute(struct slash *slash, char *line);

int slash_loop(struct slash *slash, const char *prompt_good, const char *prompt_bad);

int slash_wait_interruptible(struct slash *slash, unsigned int ms);

int slash_set_wait_interruptible(struct slash *slash, slash_waitfunc_t waitfunc);

int slash_printf(struct slash *slash, const char *format, ...);

int slash_getopt(struct slash *slash, const char *optstring);

void slash_clear_screen(struct slash *slash);

#endif /* _SLASH_H_ */
