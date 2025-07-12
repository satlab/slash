/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2014-2023 Satlab A/S <satlab@satlab.com>
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
#include <stdbool.h>

#ifdef SLASH_HAVE_TERMIOS_H
#include <termios.h>
#endif

/* Configuration */
#define SLASH_SHOW_MAX		25	/* Maximum number of commands to list */
#define SLASH_ARG_MAX		16	/* Maximum number of arguments, including command name */

/* Command flags */
#define SLASH_FLAG_HIDDEN	(1 << 0) /* Hidden and not shown in help or completion */
#define SLASH_FLAG_PRIVILEGED	(1 << 1) /* Privileged and hidden until enabled with slash_set_privileged() */

/* Macro used to map based on presence of optional arguments */
#define __slash_map_macro(_0, _1, _2, _name, ...) _name

#define __slash_command(_ident, _group, _name, _func, _args, _help, _flags, _context) \
	__attribute__((section(".slash." # _ident), used, aligned(sizeof(long)))) \
	struct slash_command _ident = { \
		.name  = #_name, \
		.parent = _group, \
		.func  = _func, \
		.args  = _args, \
		.help  = _help, \
		.flags = _flags, \
		.context = _context, \
	}

/* Top-level commands */
#define __slash_command_all(_name, _func, _args, _help, _flags, _context) \
	__slash_command(slash_cmd_ ## _name, \
			NULL, \
			_name, _func, _args, _help, _flags, _context)

#define __slash_command_flags(_name, _func, _args, _help, _flags) \
	__slash_command_all(_name, _func, _args, _help, _flags, NULL)

#define __slash_command_min(_name, _func, _args, _help) \
	__slash_command_flags(_name, _func, _args, _help, 0)

/**
 * slash_command() - Declare top-level command
 * @_name: Name of the command, without quotes.
 * @_func: Function to execute for this command.
 * @_help: Description string of the group.
 * @_flags: Optional, bitwise OR of SLASH_FLAG_XXX flags.
 * @_context: Optional, context passed to command through slash->context.
 */
#define slash_command(_name, _func, _args, _help, ...) \
	__slash_map_macro(,##__VA_ARGS__, \
			  __slash_command_all, \
			  __slash_command_flags, \
			  __slash_command_min)( \
				_name, _func, _args, _help, ##__VA_ARGS__)

/* Subcommand */
#define __slash_command_sub_all(_group, _name, _func, _args, _help, _flags, _context) \
	__slash_command(slash_cmd_ ## _group ## _ ## _name, \
			&slash_cmd_ ## _group, \
			_name, _func, _args, _help, _flags, _context)

#define __slash_command_sub_flags(_group, _name, _func, _args, _help, _flags) \
	__slash_command_sub_all(_group, _name, _func, _args, _help, _flags, NULL)

#define __slash_command_sub_min(_group, _name, _func, _args, _help) \
	__slash_command_sub_flags(_group, _name, _func, _args, _help, 0)

/**
 * slash_command_sub() - Declare sub-command
 * @_group: Name of the parent group, without quotes.
 * @_name: Name of the command, without quotes.
 * @_func: Function to execute for this command.
 * @_help: Description string of the group.
 * @_flags: Optional, bitwise OR of SLASH_FLAG_XXX flags.
 * @_context: Optional, context passed to command through slash->context.
 */
#define slash_command_sub(_group, _name, _func, _args, _help, ...) \
	__slash_map_macro(,##__VA_ARGS__, \
			  __slash_command_sub_all, \
			  __slash_command_sub_flags, \
			  __slash_command_sub_min)( \
				_group, _name, _func, _args, _help, ##__VA_ARGS__)

/* Subsubcommand */
#define __slash_command_subsub_all(_group, _subgroup, _name, _func, _args, _help, _flags, _context) \
	__slash_command(slash_cmd_ ## _group ## _ ## _subgroup ## _ ## _name,\
			&slash_cmd_ ## _group ## _ ## _subgroup, \
			_name, _func, _args, _help, _flags, _context)

#define __slash_command_subsub_flags(_group, _subgroup, _name, _func, _args, _help, _flags) \
	__slash_command_subsub_all(_group, _subgroup, _name, _func, _args, _help, _flags, NULL)

#define __slash_command_subsub_min(_group, _subgroup, _name, _func, _args, _help) \
	__slash_command_subsub_flags(_group, _subgroup, _name, _func, _args, _help, 0)

/* Subsubsubcommand */
#define __slash_command_subsubsub_all(_group, _subgroup, _subsubgroup, _name, \
				      _func, _args, _help, _flags, _context) \
	__slash_command(slash_cmd_ ## _group ## _ ## _subgroup ## _ ## _subsubgroup ## _ ## _name,\
			&slash_cmd_ ## _group ## _ ## _subgroup ## _ ## _subsubgroup, \
			_name, _func, _args, _help, _flags, _context)

#define __slash_command_subsubsub_flags(_group, _subgroup, _subsubgroup, _name, \
					_func, _args, _help, _flags) \
	__slash_command_subsubsub_all(_group, _subgroup, _subsubgroup, _name, \
				      _func, _args, _help, _flags, NULL)

#define __slash_command_subsubsub_min(_group, _subgroup, _subsubgroup, _name, \
				      _func, _args, _help) \
	__slash_command_subsubsub_flags(_group, _subgroup, _subsubgroup, _name, \
					_func, _args, _help, 0)

/**
 * slash_command_subsub() - Declare subsub-command
 * @_group: Name of the grandparent group, without quotes.
 * @_subgroup: Name of the parent group, without quotes.
 * @_name: Name of the command, without quotes.
 * @_func: Function to execute for this command.
 * @_help: Description string of the group.
 * @_flags: Optional, bitwise OR of SLASH_FLAG_XXX flags.
 * @_context: Optional, context passed to command through slash->context.
 */
#define slash_command_subsub(_group, _subgroup, _name, _func, _args, _help, ...) \
	__slash_map_macro(,##__VA_ARGS__, \
			  __slash_command_subsub_all, \
			  __slash_command_subsub_flags, \
			  __slash_command_subsub_min)( \
				_group, _subgroup, _name, _func, _args, _help, ##__VA_ARGS__)

/**
 * slash_command_subsubsub() - Declare subsubsub-command
 * @_group: Name of the great-grandparent group, without quotes.
 * @_subgroup: Name of the grandparent group, without quotes.
 * @_subsubgroup: Name of the parent group, without quotes.
 * @_name: Name of the command, without quotes.
 * @_func: Function to execute for this command.
 * @_help: Description string of the group.
 * @_flags: Optional, bitwise OR of SLASH_FLAG_XXX flags.
 * @_context: Optional, context passed to command through slash->context.
 */
#define slash_command_subsubsub(_group, _subgroup, _subsubgroup, _name, _func, _args, _help, ...) \
	__slash_map_macro(,##__VA_ARGS__, \
			  __slash_command_subsubsub_all, \
			  __slash_command_subsubsub_flags, \
			  __slash_command_subsubsub_min)( \
				_group, _subgroup, _subsubgroup, _name, _func, _args, _help, ##__VA_ARGS__)

/**
 * slash_command_group() - Declare top-level group
 * @_name: Name of the group, without quotes.
 * @_help: Description string of the group.
 * @_flags: Optional, bitwise OR of SLASH_FLAG_XXX flags.
 * @_context: Optional, context passed to command through slash->context.
 */
#define slash_command_group(_name, _help, ...) \
	slash_command(_name, NULL, NULL, _help, ##__VA_ARGS__)

/**
 * slash_command_subgroup() - Declare sub-group
 * @_group: Name of the parent group, without quotes.
 * @_name: Name of the group, without quotes.
 * @_help: Description string of the group.
 * @_flags: Optional, bitwise OR of SLASH_FLAG_XXX flags.
 * @_context: Optional, context passed to command through slash->context.
 */
#define slash_command_subgroup(_group, _name, _help, ...) \
	slash_command_sub(_group, _name, NULL, NULL, _help, ##__VA_ARGS__)

/**
 * slash_command_subsubgroup() - Declare subsub-group
 * @_group: Name of the grandparent group, without quotes.
 * @_subgroup: Name of the parent group, without quotes.
 * @_name: Name of the group, without quotes.
 * @_help: Description string of the group.
 * @_flags: Optional, bitwise OR of SLASH_FLAG_XXX flags.
 * @_context: Optional, context passed to command through slash->context.
 */
#define slash_command_subsubgroup(_group, _subgroup, _name, _help, ...) \
	slash_command_subsub(_group, _subgroup, _name, NULL, NULL, _help, ##__VA_ARGS__)

/* Deprecated but defined for backwards compatibility */
#define slash_command_ex slash_command
#define slash_command_sub_ex slash_command_sub
#define slash_command_subsub_ex slash_command_subsub
#define slash_command_group_ex slash_command_group
#define slash_command_subgroup_ex slash_command_subgroup

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

/**
 * struct slash_command - Command description.
 * @name: Name of the command.
 * @func: Pointer to the handler function for this command.
 * @args: Pointer to the argument description string or NULL if the function
 * does not take arguments.
 * @help: Pointer to the help string for the command.
 * @flags: Bitwise OR of one or more SLASH_FLAG_XXX values.
 * @context: Optional context pointer to pass to function instance.
 * @parent: Pointer to parent command or NULL if the function is a root
 * function.
 *
 * This struct should only be instantiated using the slash_command() macros.
 */
struct slash_command {
	const char *name;
	const slash_func_t func;
	const char *args;
	const char *help;
	unsigned int flags;
	void *context;
	struct slash_command *parent;
};

/**
 * struct slash_context - Slash context.
 * @original: Original termios structure for restoring terminal settings.
 * @file_write: File pointer used for output.
 * @file_read: File pointer used for input.
 * @waitfunc: Low-level function used for slash_wait_interruptible().
 * @use_activated: True if the console should require activation before use.
 * @privileged: True if the console is in privileged mode.
 * @exit_inhibit: True if exit should be inhibited in this console.
 * @line_size: Size in bytes of the line buffer.
 * @prompt: Current prompt string.
 * @prompt_length: Length in bytes of the prompt string.
 * @buffer: Pointer to line buffer memory.
 * @cursor: Current index of cursor in line buffer.
 * @cursor_screen: Current index of cursor in line buffer on screen.
 * @length: Current amount of used bytes in line buffer.
 * @length_screen: Current amount of used bytes in line buffer on screen.
 * @change_start: Index of first byte in line buffer that needs screen refresh.
 * @change_end: Index of first byte in line buffer that does not need screen refresh.
 * @refresh_full: Force a full screen refresh including prompt.
 * @last_char: Last input character.
 * @history_size: Size in byte of the history buffer.
 * @history_depth: Number of history entries browsed back.
 * @history_avail: Number of available bytes in history.
 * @history_rewind_length: Number of bytes in history buffer used for temporary
 * storage while browsing.
 * @history: Pointer to history buffer memory.
 * @history_head: Pointer to first byte of circular history buffer.
 * @history_tail: Pointer to last byte of circular history buffer.
 * @history_cursor: Current cursor when browsing history.
 * @argv: Argument vector passed to commands.
 * @argc: Number of valid arguments in argv.
 * @context: Context pointer from command registration.
 * @optarg: Pointer to current option argument.
 * @optind: Index of the first non-option argument.
 * @opterr: Print warning on unknown option or missing argument.
 * @optopt: Last option character.
 * @sp: Internal getopt parser state.
 */
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
	char *buffer;
	size_t cursor;
	size_t cursor_screen;
	size_t length;
	size_t length_screen;
	size_t change_start;
	size_t change_end;
	bool refresh_full;
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

/**
 * slash_create() - Allocate slash context and buffers.
 * @line_size: Number of bytes to allocate for the line buffer.
 * @history_size: Number of bytes to allocate for the history buffer.
 *
 * This command dynamically allocates and initializes a new slash context and buffers
 * for the command line and history. The allocated memory should be freed using
 * slash_destroy() when no longer needed.
 *
 * Return: a pointer to a new slash context, or NULL if the context could not
 * be allocated.
 */
struct slash *slash_create(size_t line_size, size_t history_size);

/**
 * slash_destroy() - Free slash context and buffers.
 * @slash: slash context to free.
 *
 * This command frees a slash context and buffers that was previously allocated
 * with slash_create().
 */
void slash_destroy(struct slash *slash);

/**
 * slash_init() - Initialize slash context and buffers.
 * @slash: slash context to initialize.
 * @line: Pointer to line buffer.
 * @line_size: Size in bytes of the line buffer.
 * @history: Pointer to history buffer.
 * @history_size: Size in bytes of the history buffer.
 *
 * This command initializes a slash context and buffers.
 *
 * Return: 0 if the initialization was successful, negative error value otherwise.
 */
int slash_init(struct slash *slash,
	       char *line, size_t line_size,
	       char *history, size_t history_size);


/**
 * slash_refresh() - Write current line buffer to terminal.
 * @slash: slash context.
 *
 * Return: 0 if the write succeeded, -1 otherwise.
 */
int slash_refresh(struct slash *slash);

/**
 * slash_reset() - Reset line buffer to empty line.
 * @slash: slash context.
 */
void slash_reset(struct slash *slash);

/**
 * slash_set_prompt() - Set prompt
 * @slash: slash context.
 * @prompt: Prompt to print before command line.
 */
void slash_set_prompt(struct slash *slash, const char *prompt);

/**
 * slash_readline() - Read line from user.
 * @slash: slash context.
 *
 * Return: Pointer to input string, NULL if the user pressed ^D.
 */
char *slash_readline(struct slash *slash);

/**
 * slash_execute() - Execute command.
 * @slash: slash context.
 * @line: Buffer with command line to execute.
 *
 * Return: Return value from executed command, or
 * -ENOENT if the command could not be found, or
 * -EISDIR if the command is a group, or
 * -EINVAL if the command contained mismatched quotes, or
 * -E2BIG if the command contained too many arguments
 */
int slash_execute(struct slash *slash, char *line);

/**
 * slash_loop() - Continuously read and execute commands.
 * @slash: slash context.
 *
 * Return: 0 if the user exited the console, or
 * -ENOTTY if the terminal could not be configured.
 */
int slash_loop(struct slash *slash);

/**
 * slash_wait_interruptible() - Wait for keypress.
 * @slash: slash context.
 * @ms: Maximum number of milliseconds to wait.
 *
 * Return: return valid from waitfunc or -ENOSYS if no waitfunc has been specified.
 */
int slash_wait_interruptible(struct slash *slash, unsigned int ms);

/**
 * slash_set_wait_interruptible() - Set wait function.
 * @slash: slash context.
 * @waitfunc: New wait function.
 *
 * The wait function should match the slash_waitfunc_t prototype and return the
 * received character or -ETIMEDOUT if no character was received before the
 * timeout expired.
 *
 * Return: This function always succeeds and returns 0.
 */
int slash_set_wait_interruptible(struct slash *slash, slash_waitfunc_t waitfunc);

/**
 * slash_printf() - Print formatted data.
 * @slash: slash context.
 * @format: Format string.
 *
 * This function prints formatted string to the output file pointer.
 *
 * Return: This function is just a wrapper around printf and thus has the same
 * return value.
 */
int slash_printf(struct slash *slash, const char *format, ...);

/**
 * slash_getop() - Parse command-line options
 * @slash: slash context.
 * @optstring: Options string.
 *
 * This function walks the slash->argv[] array, looking for optional arguments.
 * The optstring is a sequence of option characters to look for, each one
 * optionally followed by ':' to indicate that the option expects an argument.
 *
 * Return: For each found option, the option character is returned. If an
 * invalid option character is found, or a option character is missing an
 * argument, '?' is returned. When no more options are found, -1 is returned.
 */
int slash_getopt(struct slash *slash, const char *optstring);

/**
 * slash_clear_screen() - Clear the screen.
 * @slash: slash context.
 *
 * Clears the screen using terminal escape sequence.
 */
void slash_clear_screen(struct slash *slash);

/**
 * slash_require_activation() - Set activation requirement.
 * @slash: slash context.
 * @activate: Set to true if the terminal should require activation before use.
 */
void slash_require_activation(struct slash *slash, bool activate);

/**
 * slash_inhibit_exit() - Inhibit exit command.
 * @slash: slash context.
 * @activate: Set to true if the exit command should be inhibited.
 */
void slash_inhibit_exit(struct slash *slash, bool inhibit);

/**
 * slash_set_privileged() - Set current privileged level.
 * @slash: slash context.
 * @activate: Set to true to allow privileged commands.
 */
void slash_set_privileged(struct slash *slash, bool privileged);

#endif /* _SLASH_H_ */
