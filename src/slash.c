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

#include <slash/slash.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>

#ifdef SLASH_HAVE_TERMIOS_H
#include <termios.h>
#endif

#ifdef SLASH_HAVE_SELECT
#include <sys/select.h>
#endif

/* Configuration */
#define SLASH_ARG_MAX		16	/* Maximum number of arguments */
#define SLASH_SHOW_MAX		25	/* Maximum number of commands to list */

/* Terminal codes */
#define ESC '\x1b'
#define DEL '\x7f'

#define CONTROL(code) (code - '@')
#define ESCAPE(code) "\x1b[0" code
#define ESCAPE_NUM(code) "\x1b[%u" code

/* Command-line option parsing */
int slash_getopt(struct slash *slash, char *opts)
{
	/* From "public domain AT&T getopt source" newsgroup posting */
	int c;
	char *cp;

	if (slash->sp == 1) {
		if (slash->optind >= slash->argc ||
		    slash->argv[slash->optind][0] != '-' ||
		    slash->argv[slash->optind][1] == '\0') {
			return EOF;
		} else if (!strcmp(slash->argv[slash->optind], "--")) {
			slash->optind++;
			return EOF;
		}
	}

	slash->optopt = c = slash->argv[slash->optind][slash->sp];

	if (c == ':' || (cp = strchr(opts, c)) == NULL) {
		slash_printf(slash, "Unknown option -%c\n", c);
		if (slash->argv[slash->optind][++(slash->sp)] == '\0') {
			slash->optind++;
			slash->sp = 1;
		}
		return '?';
	}

	if (*(++cp) == ':') {
		if (slash->argv[slash->optind][slash->sp+1] != '\0') {
			slash->optarg = &slash->argv[(slash->optind)++][slash->sp+1];
		} else if(++(slash->optind) >= slash->argc) {
			slash_printf(slash, "Option -%c requires an argument\n", c);
			slash->sp = 1;
			return '?';
		} else {
			slash->optarg = slash->argv[(slash->optind)++];
		}
		slash->sp = 1;
	} else {
		if (slash->argv[slash->optind][++(slash->sp)] == '\0') {
			slash->sp = 1;
			slash->optind++;
		}
		slash->optarg = NULL;
	}

	return c;
}

/* Terminal handling */
static size_t slash_escaped_strlen(const char *s)
{
	int len = 0;
	bool escaped = false;

	while (*s) {
		if (escaped) {
			if (*s == 'm')
				escaped = false;
		} else if (*s == ESC) {
			escaped = true;
		} else {
			len++;
		}
		s++;
	}

	return len;
}

static int slash_rawmode_enable(struct slash *slash)
{
#ifdef SLASH_HAVE_TERMIOS_H
	struct termios raw;

	if (tcgetattr(slash->fd_read, &slash->original) < 0)
		return -ENOTTY;

	raw = slash->original;
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;

	if (tcsetattr(slash->fd_read, TCSANOW, &raw) < 0)
		return -ENOTTY;
#endif
	return 0;
}

static int slash_rawmode_disable(struct slash *slash)
{
#ifdef SLASH_HAVE_TERMIOS_H
	if (tcsetattr(slash->fd_read, TCSANOW, &slash->original) < 0)
		return -ENOTTY;
#endif
	return 0;
}

static void slash_atexit(void)
{
	// FIXME slash_rawmode_disable(slash);
}

static int slash_configure_term(struct slash *slash)
{
	if (slash_rawmode_enable(slash) < 0)
		return -ENOTTY;

	slash->rawmode = true;

	if (!slash->atexit_registered) {
		atexit(slash_atexit);
		slash->atexit_registered = true;
	}

	return 0;
}

static int slash_restore_term(struct slash *slash)
{
	if (slash_rawmode_disable(slash) < 0)
		return -ENOTTY;

	slash->rawmode = false;

	return 0;
}

static int slash_write(struct slash *slash, const char *buf, size_t count)
{
	return write(slash->fd_write, buf, count);
}

static int slash_read(struct slash *slash, void *buf, size_t count)
{
	return read(slash->fd_read, buf, count);
}

static int slash_putchar(struct slash *slash, char c)
{
	return slash_write(slash, &c, 1);
}

static int slash_getchar(struct slash *slash)
{
	unsigned char c;

	if (slash_read(slash, &c, 1) < 1)
		return -EIO;

	return c;
}

#ifdef SLASH_HAVE_SELECT
static int slash_wait_select(struct slash *slash, unsigned int ms)
{
	int ret = 0;
	char c;
	fd_set fds;
	struct timeval timeout;

	timeout.tv_sec = ms / 1000;
	timeout.tv_usec = ms - timeout.tv_sec * 1000;

	FD_ZERO(&fds);
	FD_SET(slash->fd_read, &fds);

	fcntl(slash->fd_read, F_SETFL, fcntl(slash->fd_read, F_GETFL) |  O_NONBLOCK);

	ret = select(1, &fds, NULL, NULL, &timeout);
	if (ret == 1) {
		ret = -EINTR;
		slash_read(slash, &c, 1);
	}

	fcntl(slash->fd_read, F_SETFL, fcntl(slash->fd_read, F_GETFL) & ~O_NONBLOCK);

	return ret;
}
#endif

int slash_set_wait_interruptible(struct slash *slash, slash_waitfunc_t waitfunc)
{
	slash->waitfunc = waitfunc;
	return 0;
}

int slash_wait_interruptible(struct slash *slash, unsigned int ms)
{
	if (slash->waitfunc)
		return slash->waitfunc(slash, ms);

	return -ENOSYS;
}

int slash_printf(struct slash *slash, const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);

	ret = vdprintf(slash->fd_write, format, args);

	va_end(args);

	return ret;
}

static void slash_bell(struct slash *slash)
{
	slash_putchar(slash, '\a');
}

static bool slash_line_empty(char *line, size_t linelen)
{
	while (*line && linelen--)
		if (!isspace(*line++))
			return false;

	return true;
}

/* Command handling */
static int slash_command_compare(struct slash_command *c1,
				 struct slash_command *c2)
{
	/* Compare names alphabetically */
	return strcmp(c1->name, c2->name);
}

static int slash_command_init(struct slash_command *cmd)
{
	char *p = cmd->name;

	/* Initialize subcommand list */
	slash_list_init(&cmd->sub);

	/* Replace underscore with dash */
	while (*p) {
		if (*p == '_')
			*p = '-';
		p++;
	}

	return 0;
}

static int slash_command_register(struct slash *slash,
				  struct slash_command *cmd,
				  struct slash_command *super)
{
	struct slash_list *list;
	struct slash_command *cur;

	list = super ? &super->sub : &slash->commands;

	slash_command_init(cmd);

	/* Insert sorted by name */
	slash_list_for_each(cur, list, command) {
		if (slash_command_compare(cur, cmd) > 0) {
			slash_list_insert_tail(&cur->command, &cmd->command);
			break;
		}
	}

	/* Insert as last member */
	if (slash_list_head(list, &cur->command))
		slash_list_insert_tail(list, &cmd->command);

	return 0;
}

static char *slash_command_line_token(char *line, size_t *len, char **next)
{
	char *token;

	/* Skip leading whitespace */
	while (*line && *line == ' ')
		line++;

	token = line;
	*len = 0;

	while (*line && *line != ' ') {
		(*len)++;
		line++;
	}

	/* Skip trailing whitespace */
	while (*line && *line == ' ')
		line++;

	*next = line;

	return *len ? token : NULL;
}

static struct slash_command *
slash_command_find(struct slash *slash, char *line, size_t linelen, char **args)
{
	struct slash_list *start = &slash->commands;
	struct slash_command *cur, *command = NULL;
	char *next = line, *token;
	size_t tokenlen, matchlen;
	bool found;

	while (!slash_list_empty(start) &&
	      (token = slash_command_line_token(next, &tokenlen, &next))) {

		if (token >= line + linelen)
			break;

		found = false;

		slash_list_for_each(cur, start, command) {
			matchlen = slash_max(tokenlen, strlen(cur->name));
			if (!strncmp(token, cur->name, matchlen)) {
				command = cur;
				*args = token;
				found = true;
			}
		}

		if (!found)
			break;

		start = &command->sub;
	}

	return command;
}

static int slash_build_args(char *args, char **argv, int *argc)
{
	/* Quote level */
	enum {
		SLASH_QUOTE_NONE,
		SLASH_QUOTE_SINGLE,
		SLASH_QUOTE_DOUBLE,
	} quote = SLASH_QUOTE_NONE;

	*argc = 0;

	while (*args && *argc < SLASH_ARG_MAX) {
		/* Check for quotes */
		if (*args == '\'') {
			quote = SLASH_QUOTE_SINGLE;
			args++;
		} else if (*args == '\"') {
			quote = SLASH_QUOTE_DOUBLE;
			args++;
		}

		/* Argument starts here */
		argv[(*argc)++] = args;

		/* Loop over input argument */
		while (*args) {
			if (quote == SLASH_QUOTE_SINGLE && *args == '\'') {
				quote = SLASH_QUOTE_NONE;
				break;
			} else if (quote == SLASH_QUOTE_DOUBLE &&
				   *args == '\"') {
				quote = SLASH_QUOTE_NONE;
				break;
			} else if (quote == SLASH_QUOTE_NONE && *args == ' ') {
				break;
			}

			args++;
		}

		/* End argument with zero byte */
		if (*args)
			*args++ = '\0';

		/* Skip trailing white space */
		while (*args && *args == ' ')
			args++;
	}

	/* Test for quote mismatch */
	if (quote != SLASH_QUOTE_NONE)
		return -1;

	/* According to C11 section 5.1.2.2.1, argv[argc] must be NULL */
	argv[*argc] = NULL;

	return 0;
}

static void strprepend(char *dest, char *src)
{
	int len = strlen(src);
	memmove(dest + len, dest, strlen(dest) + 1);
	memcpy(dest, src, len);
}

static void slash_command_fullname(struct slash_command *command, char *name)
{
	name[0] = '\0';

	while (command != NULL) {
		/* Prepend command name to full name */
		strprepend(name, command->name);
		command = command->group;
		if (command)
			strprepend(name, " ");
	};
}

static void slash_command_usage(struct slash *slash, struct slash_command *command)
{
	char fullname[slash->line_size];
	const char *args = command->args ? command->args : "";
	const char *type = command->func ? "usage" : "group";

	fullname[0] = '\0';

	slash_command_fullname(command, fullname);

	slash_printf(slash, "%s: %s %s\n", type, fullname, args);
}

static void slash_command_description(struct slash *slash, struct slash_command *command)
{
	char *nl;
	const char *help = "";
	int desclen = 0;

	/* Extract first line from help as description */
	if (command->help != NULL) {
		help = command->help;
		nl = strchr(help, '\n');
		desclen = nl ? nl - help : strlen(help);
	}

	slash_printf(slash, "%-15s %.*s\n", command->name, desclen, help);
}

static void slash_command_help(struct slash *slash, struct slash_command *command)
{
	const char *help = "";
	struct slash_command *cur;

	if (command->help != NULL)
		help = command->help;

	slash_command_usage(slash, command);
	slash_printf(slash, "%s", help);

	if (help[strlen(help)-1] != '\n')
		slash_printf(slash, "\n");

	if (!slash_list_empty(&command->sub)) {
		slash_printf(slash,
			     "\nAvailable subcommands in \'%s\' group:\n",
			     command->name);
		slash_list_for_each(cur, &command->sub, command)
			slash_command_description(slash, cur);
	}
}

int slash_execute(struct slash *slash, char *line)
{
	struct slash_command *command, *cur;
	char *args, *argv[SLASH_ARG_MAX];
	int ret, argc = 0;

	command = slash_command_find(slash, line, strlen(line), &args);
	if (!command) {
		slash_printf(slash, "No such command: %s\n", line);
		return -ENOENT;
	}

	if (!command->func) {
		slash_printf(slash, "Available subcommands in \'%s\' group:\n",
			     command->name);
		slash_list_for_each(cur, &command->sub, command)
			slash_command_description(slash, cur);
		return -EINVAL;
	}

	/* Build args */
	if (slash_build_args(args, argv, &argc) < 0) {
		slash_printf(slash, "Mismatched quotes\n");
		return -EINVAL;
	}

	/* Reset state for slash_getopt */
	slash->optarg = 0;
	slash->optind = 1;
	slash->opterr = 1;
	slash->optopt = '?';
	slash->sp = 1;

	slash->argc = argc;
	slash->argv = argv;
	ret = command->func(slash);

	if (ret == SLASH_EUSAGE)
		slash_command_usage(slash, command);

	return ret;
}

/* Completion */
static char *slash_last_word(char *line, size_t len, size_t *lastlen)
{
	char *word;

	word = &line[len];
	*lastlen = 0;

	while (word > line && *word != ' ') {
		word--;
		(*lastlen)++;
	}

	if (word != line) {
		word++;
		(*lastlen)--;
	}

	return word;
}

static void slash_show_completions(struct slash *slash, struct slash_list *completions)
{
	struct slash_command *cur;

	slash_list_for_each(cur, completions, completion)
		slash_command_description(slash, cur);
}

static bool slash_complete_confirm(struct slash *slash, int matches)
{
	char c = 'y';

	if (matches <= SLASH_SHOW_MAX)
		return true;

	slash_printf(slash, "Display all %d possibilities? (y or n) ", matches);
	fflush(stdout);
	do {
		if (c != 'y')
			slash_bell(slash);
		c = slash_getchar(slash);
	} while (c != 'y' && c != 'n' && c != '\t' &&
		(isprint((int)c) || isspace((int)c)));

	slash_printf(slash, "\n");

	return (c == 'y' || c == '\t');
}

static int slash_prefix_length(char *s1, char *s2)
{
	int len = 0;

	while (*s1 && *s2 && *s1 == *s2) {
		len++;
		s1++;
		s2++;
	}

	return len;
}

static void slash_set_completion(struct slash *slash,
				 char *complete, char *match,
				 int len, bool space)
{
	strncpy(complete, match, len);
	complete[len] = '\0';
	if (space)
		strncat(complete, " ", slash->line_size - 1);
	slash->cursor = slash->length = strlen(slash->buffer);
}

static void slash_complete(struct slash *slash)
{
	int matches = 0;
	size_t completelen = 0, commandlen = 0, prefixlen = -1;
	char *complete, *args;
	struct slash_list *search = &slash->commands;
	struct slash_command *cur, *command = NULL, *prefix = NULL;
	SLASH_LIST(completions);

	/* Find start of word to complete */
	complete = slash_last_word(slash->buffer, slash->cursor, &completelen);
	commandlen = complete - slash->buffer;

	/* Determine if we are completing sub command */
	if (!slash_line_empty(slash->buffer, commandlen)) {
		command = slash_command_find(slash, slash->buffer, commandlen, &args);
		if (command)
			search = &command->sub;
	}

	/* Search list for matches */
	slash_list_for_each(cur, search, command) {
		if (strncmp(cur->name, complete, completelen) != 0)
			continue;

		slash_list_insert_tail(&completions, &cur->completion);
		matches++;

		/* Find common prefix */
		if (prefixlen == -1) {
			prefix = cur;
			prefixlen = strlen(prefix->name);
		} else {
			prefixlen = slash_prefix_length(prefix->name, cur->name);
		}
	}

	/* Complete or list matches */
	if (!matches) {
		if (command) {
			slash_printf(slash, "\n");
			slash_command_usage(slash, command);
		} else {
			slash_bell(slash);
		}
	} else if (matches == 1) {
		slash_set_completion(slash, complete, prefix->name, prefixlen, true);
	} else if (slash->last_char != '\t') {
		slash_set_completion(slash, complete, prefix->name, prefixlen, false);
		slash_bell(slash);
	} else {
		slash_printf(slash, "\n");
		if (slash_complete_confirm(slash, matches))
			slash_show_completions(slash, &completions);
	}
}

/* History */
static char *slash_history_increment(struct slash *slash, char *ptr)
{
	if (++ptr > &slash->history[slash->history_size-1])
		ptr = slash->history;
	return ptr;
}

static char *slash_history_decrement(struct slash *slash, char *ptr)
{
	if (--ptr < slash->history)
		ptr = &slash->history[slash->history_size-1];
	return ptr;
}

static void slash_history_push_head(struct slash *slash)
{
	*slash->history_head = '\0';
	slash->history_head = slash_history_increment(slash, slash->history_head);
	slash->history_avail++;
}

static void slash_history_push_tail(struct slash *slash, char c)
{
	*slash->history_tail = c;
	slash->history_tail = slash_history_increment(slash, slash->history_tail);
	slash->history_avail--;
}

static void slash_history_pull_tail(struct slash *slash)
{
	*slash->history_tail = '\0';
	slash->history_tail = slash_history_decrement(slash, slash->history_tail);
	slash->history_avail++;
}

static size_t slash_history_strlen(struct slash *slash, char *ptr)
{
	size_t len = 0;

	while (*ptr) {
		ptr = slash_history_increment(slash, ptr);
		len++;
	}

	return len;
}


static void slash_history_copy(struct slash *slash, char *dst, char *src, size_t len)
{
	while (len--) {
		*dst++ = *src;
		src = slash_history_increment(slash, src);
	}
}

static char *slash_history_search_back(struct slash *slash,
				       char *start, size_t *startlen)
{
	if (slash->history_cursor == slash->history_head)
		return NULL;

	/* Skip first two trailing zeros */
	start = slash_history_decrement(slash, start);
	start = slash_history_decrement(slash, start);

	while (*start) {
		if (start == slash->history_head)
			break;
		start = slash_history_decrement(slash, start);
	}

	/* Skip leading zero */
	if (start != slash->history_head)
		start = slash_history_increment(slash, start);

	*startlen = slash_history_strlen(slash, start);

	return start;
}

static char *slash_history_search_forward(struct slash *slash,
					  char *start, size_t *startlen)
{
	if (slash->history_cursor == slash->history_tail)
		return NULL;

	while (*start) {
		start = slash_history_increment(slash, start);
		if (start == slash->history_tail)
			return NULL;
	}

	/* Skip trailing zero */
	start = slash_history_increment(slash, start);

	if (start == slash->history_tail)
		*startlen = 0;
	else
		*startlen = slash_history_strlen(slash, start);

	return start;
}

static void slash_history_pull(struct slash *slash, size_t len)
{
	while (len-- > 0)
		slash_history_push_head(slash);
	while (*slash->history_head != '\0')
		slash_history_push_head(slash);

	/* Push past final zero byte */
	slash_history_push_head(slash);
}

static void slash_history_push(struct slash *slash, char *buf, size_t len)
{
	/* Remove oldest entry until space is available */
	if (len > slash->history_avail)
		slash_history_pull(slash, len - slash->history_avail);

	/* Copy to history */
	while (len--)
		slash_history_push_tail(slash, *buf++);

	slash->history_cursor = slash->history_tail;
}

static void slash_history_rewind(struct slash *slash, size_t len)
{
	while (len-- > 0)
		slash_history_pull_tail(slash);

	*slash->history_tail = '\0';

	slash->history_rewind_length = 0;
}

static void slash_history_add(struct slash *slash, char *line)
{
	/* Check if we are browsing history and clear latest entry */
	if (slash->history_depth != 0 && slash->history_rewind_length != 0)
		slash_history_rewind(slash, slash->history_rewind_length);

	/* Reset history depth */
	slash->history_depth = 0;
	slash->history_rewind_length = 0;
	slash->history_cursor = slash->history_tail;

	/* Push including trailing zero */
	if (!slash_line_empty(line, strlen(line)))
		slash_history_push(slash, line, strlen(line) + 1);
}

static void slash_history_next(struct slash *slash)
{
	char *src;
	size_t srclen;

	src = slash_history_search_forward(slash, slash->history_cursor, &srclen);
	if (!src)
		return;

	slash->history_depth--;
	slash_history_copy(slash, slash->buffer, src, srclen);
	slash->buffer[srclen] = '\0';
	slash->history_cursor = src;
	slash->cursor = slash->length = srclen;

	/* Rewind if use to store buffer temporarily */
	if (!slash->history_depth && slash->history_cursor != slash->history_tail)
		slash_history_rewind(slash, slash->history_rewind_length);
}

static void slash_history_previous(struct slash *slash)
{
	char *src;
	size_t srclen, buflen;

	src = slash_history_search_back(slash, slash->history_cursor, &srclen);
	if (!src)
		return;

	/* Store current buffer temporarily */
	buflen = strlen(slash->buffer);
	if (!slash->history_depth && buflen) {
		slash_history_add(slash, slash->buffer);
		slash->history_rewind_length = buflen + 1;
	}

	slash->history_depth++;
	slash_history_copy(slash, slash->buffer, src, srclen);
	slash->buffer[srclen] = '\0';
	slash->history_cursor = src;
	slash->cursor = slash->length = srclen;
}

/* Line editing */
static void slash_insert(struct slash *slash, int c)
{
	if (slash->length + 1 < slash->line_size) {
		memmove(&slash->buffer[slash->cursor + 1],
			&slash->buffer[slash->cursor],
			slash->length - slash->cursor);
		slash->buffer[slash->cursor] = c;
		slash->cursor++;
		slash->length++;
		slash->buffer[slash->length] = '\0';
	}
}

static int slash_refresh(struct slash *slash)
{
	char esc[16];

	/* Ensure line is zero terminated */
	slash->buffer[slash->length] = '\0';

	/* Move cursor to left edge */
	snprintf(esc, sizeof(esc), "\r");
	if (slash_write(slash, esc, strlen(esc)) < 0)
		return -1;

	/* Write the prompt and the current buffer content */
	if (slash_write(slash, slash->prompt, slash->prompt_length) < 0)
		return -1;
	if (slash_write(slash, slash->buffer, slash->length) < 0)
		return -1;

	/* Erase to right */
	snprintf(esc, sizeof(esc), ESCAPE("K"));
	if (slash_write(slash, esc, strlen(esc)) < 0)
		return -1;

	/* Move cursor to original position. */
	snprintf(esc, sizeof(esc), "\r" ESCAPE_NUM("C"),
		(unsigned int)(slash->cursor + slash->prompt_print_length));
	if (slash_write(slash, esc, strlen(esc)) < 0)
		return -1;

	return 0;
}

static void slash_reset(struct slash *slash)
{
	slash->buffer[0] = '\0';
	slash->length = 0;
	slash->cursor = 0;
}

static void slash_arrow_up(struct slash *slash)
{
	slash_history_previous(slash);
}

static void slash_arrow_down(struct slash *slash)
{
	slash_history_next(slash);
}

static void slash_arrow_right(struct slash *slash)
{
	if (slash->cursor < slash->length)
		slash->cursor++;
}

static void slash_arrow_left(struct slash *slash)
{
	if (slash->cursor > 0)
		slash->cursor--;
}

static void slash_delete(struct slash *slash)
{
	if (slash->cursor < slash->length) {
		slash->length--;
		memmove(&slash->buffer[slash->cursor],
			&slash->buffer[slash->cursor + 1], slash->length - slash->cursor);
		slash->buffer[slash->length] = '\0';
	}
}

void slash_clear_screen(struct slash *slash)
{
	char *esc = ESCAPE("H") ESCAPE("2J");
	slash_write(slash, esc, strlen(esc));
}

static void slash_backspace(struct slash *slash)
{
	if (slash->cursor > 0) {
		slash->cursor--;
		slash->length--;
		memmove(&slash->buffer[slash->cursor],
			&slash->buffer[slash->cursor + 1], slash->length - slash->cursor);
		slash->buffer[slash->length] = '\0';
	}
}

static void slash_delete_word(struct slash *slash)
{
	int old_cursor = slash->cursor, erased;

	while (slash->cursor > 0 && slash->buffer[slash->cursor-1] == ' ')
		slash->cursor--;
	while (slash->cursor > 0 && slash->buffer[slash->cursor-1] != ' ')
		slash->cursor--;

	erased = old_cursor - slash->cursor;

	memmove(slash->buffer + slash->cursor, slash->buffer + old_cursor, erased);
	slash->length -= erased;
}

static void slash_swap(struct slash *slash)
{
	char tmp;

	if (slash->cursor > 0 && slash->cursor < slash->length) {
		tmp = slash->buffer[slash->cursor-1];
		slash->buffer[slash->cursor-1] = slash->buffer[slash->cursor];
                slash->buffer[slash->cursor] = tmp;
                if (slash->cursor != slash->length-1)
			slash->cursor++;
	}
}

char *slash_readline(struct slash *slash, const char *prompt)
{
	char *ret = slash->buffer;
	int c, esc[3];
	bool done = false, escaped = false;

	slash->prompt = prompt;
	slash->prompt_length = strlen(prompt);
	slash->prompt_print_length = slash_escaped_strlen(prompt);

	/* Reset buffer */
	slash_reset(slash);
	slash_refresh(slash);

	while (!done && ((c = slash_getchar(slash)) >= 0)) {
		if (escaped) {
			esc[0] = c;
			esc[1] = slash_getchar(slash);

			if (esc[0] == '[' && esc[1] == 'A') {
				slash_arrow_up(slash);
			} else if (esc[0] == '[' && esc[1] == 'B') {
				slash_arrow_down(slash);
			} else if (esc[0] == '[' && esc[1] == 'C') {
				slash_arrow_right(slash);
			} else if (esc[0] == '[' && esc[1] == 'D') {
				slash_arrow_left(slash);
			} else if (esc[0] == '[' && (esc[1] > '0' &&
						     esc[1] < '7')) {
				esc[2] = slash_getchar(slash);
				if (esc[1] == '3' && esc[2] == '~')
					slash_delete(slash);
			} else if (esc[0] == 'O' && esc[1] == 'H') {
				slash->cursor = 0;
			} else if (esc[0] == 'O' && esc[1] == 'F') {
				slash->cursor = slash->length;
			} else if (esc[0] == '1' && esc[1] == '~') {
				slash->cursor = 0;
			} else if (esc[0] == '4' && esc[1] == '[') {
				esc[2] = slash_getchar(slash);
				if (esc[2] == '~')
					slash->cursor = slash->length;
			}
			escaped = false;
		} else if (iscntrl(c)) {
			switch (c) {
			case CONTROL('A'):
				slash->cursor = 0;
				break;
			case CONTROL('B'):
				slash_arrow_left(slash);
				break;
			case CONTROL('C'):
				slash_reset(slash);
				done = true;
				break;
			case CONTROL('D'):
				if (slash->length > 0) {
					slash_delete(slash);
				} else {
					ret = NULL;
					done = true;
				}
				break;
			case CONTROL('E'):
				slash->cursor = slash->length;
				break;
			case CONTROL('F'):
				slash_arrow_right(slash);
				break;
			case CONTROL('K'):
				slash->length = slash->cursor;
				break;
			case CONTROL('L'):
				slash_clear_screen(slash);
				break;
			case CONTROL('N'):
				slash_arrow_down(slash);
				break;
			case CONTROL('P'):
				slash_arrow_up(slash);
				break;
			case CONTROL('T'):
				slash_swap(slash);
				break;
			case CONTROL('U'):
				slash->cursor = 0;
				slash->length = 0;
				break;
			case CONTROL('W'):
				slash_delete_word(slash);
				break;
			case '\t':
				slash_complete(slash);
				break;
			case '\r':
			case '\n':
				done = true;
				break;
			case '\b':
			case DEL:
				slash_backspace(slash);
				break;
			case ESC:
				escaped = true;
				break;
			default:
				/* Unknown control */
				break;
			}
		} else if (isprint(c)) {
			/* Add to buffer */
			slash_insert(slash, c);
		}

		slash->last_char = c;

		if (!done)
			slash_refresh(slash);
	}

	slash_putchar(slash, '\n');
	slash_history_add(slash, slash->buffer);

	return ret;
}

/* Builtin commands */
static int slash_builtin_help(struct slash *slash)
{
	char *args;
	char find[slash->line_size];
	int i, available = sizeof(find);
	struct slash_command *command;

	/* If no arguments given, just list all top-level commands */
	if (slash->argc < 2) {
		slash_printf(slash, "Available commands:\n");
		slash_list_for_each(command, &slash->commands, command)
			slash_command_description(slash, command);
		return SLASH_SUCCESS;
	}

	find[0] = '\0';

	for (i = 1; i < slash->argc; i++) {
		if (strlen(slash->argv[i]) >= available)
			return SLASH_ENOSPC;
		strcat(find, slash->argv[i]);
		strcat(find, " ");
	}
	command = slash_command_find(slash, find, strlen(find), &args);
	if (!command) {
		slash_printf(slash, "No such command: %s\n", find);
		return SLASH_EINVAL;
	}

	slash_command_help(slash, command);

	return SLASH_SUCCESS;
}
slash_command(help, slash_builtin_help, "[command]",
	      "Show available commands");

static int slash_builtin_history(struct slash *slash)
{
	char *p = slash->history_head;

	while (p != slash->history_tail) {
		slash_putchar(slash, *p ? *p : '\n');
		p = slash_history_increment(slash, p);
	}

	return SLASH_SUCCESS;
}
slash_command(history, slash_builtin_history, NULL,
	      "Show previous commands");

#ifndef SLASH_NO_EXIT
static int slash_builtin_exit(struct slash *slash)
{
	return SLASH_EXIT;
}
slash_command(exit, slash_builtin_exit, NULL,
	      "Exit slash");
#endif

/* Core */
int slash_loop(struct slash *slash, const char *prompt_good, const char *prompt_bad)
{
	int ret;
	char *line;
	const char *prompt = prompt_good;

	if (slash_configure_term(slash) < 0)
		return -ENOTTY;

	while ((line = slash_readline(slash, prompt))) {
		if (!slash_line_empty(line, strlen(line))) {
			/* Run command */
			ret = slash_execute(slash, line);
			if (ret == SLASH_EXIT)
				break;

			/* Update prompt if enabled */
			if (prompt_bad && ret != SLASH_SUCCESS)
				prompt = prompt_bad;
			else
				prompt = prompt_good;
		} else {
			prompt = prompt_good;
		}
	}

	slash_restore_term(slash);

	return 0;
}

struct slash *slash_create(size_t line_size, size_t history_size)
{
	struct slash *slash;
	struct slash_command *cmd;
	unsigned long command_size;

	/* Created by GCC before and after slash ELF section */
	extern struct slash_command __start_slash;
	extern struct slash_command __stop_slash;

	/* Allocate slash context */
	slash = calloc(sizeof(*slash), 1);
	if (!slash)
		return NULL;

	/* Setup default values */
	slash->fd_read = STDIN_FILENO;
	slash->fd_write = STDOUT_FILENO;
#ifdef SLASH_HAVE_SELECT
	slash->waitfunc = slash_wait_select;
#endif

	/* Calculate command section size */
	command_size = labs((long)&slash_cmd_help -
			    (long)&slash_cmd_history);

	/* Allocate zero-initialized line and history buffers */
	slash->line_size = line_size;
	slash->buffer = calloc(slash->line_size, 1);
	if (!slash->buffer) {
		free(slash);
		return NULL;
	}

	slash->history_size = history_size;
	slash->history = calloc(slash->history_size, 1);
	if (!slash->history) {
		free(slash->buffer);
		free(slash);
		return NULL;
	}

	/* Initialize history */
	slash->history_head = slash->history;
	slash->history_tail = slash->history;
	slash->history_cursor = slash->history;
	slash->history_avail = slash->history_size - 1;

#define slash_for_each_command(_c) \
	for (_c = &__stop_slash-1; \
	     _c >= &__start_slash; \
	     _c = (struct slash_command *)((char *)_c - command_size))

	/* Register commands */
	slash_list_init(&slash->commands);
	slash_for_each_command(cmd)
		slash_command_register(slash, cmd, cmd->group);

	return slash;
}

void slash_destroy(struct slash *slash)
{
	if (slash->buffer) {
		free(slash->buffer);
		slash->buffer = NULL;
	}
	if (slash->history) {
		free(slash->history);
		slash->history = NULL;
	}
}
