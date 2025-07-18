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

#include <slash/slash.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
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

/* Terminal codes */
#define ESC '\x1b'
#define DEL '\x7f'

#define CONTROL(code) (code - '@')
#define ESCAPE(code) "\x1b[" code

/* Created by GCC before and after slash ELF section */
extern struct slash_command __start_slash;
extern struct slash_command __stop_slash;

#define slash_command_list_for_each(cmd)	\
	for (cmd = &__start_slash;		\
	     cmd < &__stop_slash;		\
	     cmd++)

/* Command-line option parsing */
int slash_getopt(struct slash *slash, const char *opts)
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
		if (slash->opterr)
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
			if (slash->opterr)
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
static int slash_rawmode_enable(struct slash *slash)
{
#ifdef SLASH_HAVE_TERMIOS_H
	int fd = fileno(slash->file_read);

	if (!isatty(fd))
		return 0;

	struct termios raw;

	if (tcgetattr(fd, &slash->original) < 0)
		return -ENOTTY;

	raw = slash->original;
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSANOW, &raw) < 0)
		return -ENOTTY;
#endif
	return 0;
}

static int slash_rawmode_disable(struct slash *slash)
{
#ifdef SLASH_HAVE_TERMIOS_H
	int fd = fileno(slash->file_read);

	if (!isatty(fd))
		return 0;

	if (tcsetattr(fd, TCSANOW, &slash->original) < 0)
		return -ENOTTY;
#endif
	return 0;
}

static int slash_configure_term(struct slash *slash)
{
	if (slash_rawmode_enable(slash) < 0)
		return -ENOTTY;

	return 0;
}

static int slash_restore_term(struct slash *slash)
{
	if (slash_rawmode_disable(slash) < 0)
		return -ENOTTY;

	return 0;
}

static int slash_write(struct slash *slash, const char *buf, size_t count)
{
	return fwrite(buf, 1, count, slash->file_write) == count ? (int)count : -1;
}

static int slash_write_flush(struct slash *slash)
{
	return fflush(slash->file_write) == 0 ? 0 : -1;
}

static int slash_read(struct slash *slash, void *buf, size_t count)
{
	return fread(buf, 1, count, slash->file_read) == count ? (int)count : -1;
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

static void slash_mark_changed(struct slash *slash, size_t start, size_t end)
{
	if (slash->change_start == slash->change_end) {
		slash->change_start = start;
		slash->change_end = end;
	} else {
		if (start < slash->change_start)
			slash->change_start = start;
		if (end > slash->change_end)
			slash->change_end = end;
	}
}

#ifdef SLASH_HAVE_SELECT
static int slash_wait_select(struct slash *slash, unsigned int ms)
{
	int ret = -ETIMEDOUT, fd;
	char c;
	fd_set fds;
	struct timeval timeout;

	timeout.tv_sec = ms / 1000;
	timeout.tv_usec = (ms - timeout.tv_sec * 1000) * 1000;

	fd = fileno(slash->file_read);
	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) |  O_NONBLOCK);

	ret = select(1, &fds, NULL, NULL, &timeout);
	if (ret == 1) {
		slash_read(slash, &c, 1);
		ret = (unsigned char)c;
	}

	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

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

	ret = vfprintf(slash->file_write, format, args);

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
		if (!isspace((unsigned int) *line++))
			return false;

	return true;
}

static bool slash_line_empty_or_comment(char *line, size_t linelen)
{
	while (*line && linelen--) {
		if (*line == '#')
			return true;
		if (!isspace((unsigned int) *line++))
			return false;
	}

	return true;
}

/* Command handling */
void slash_set_privileged(struct slash *slash, bool privileged)
{
	slash->privileged = privileged;
}

static bool slash_command_is_hidden(const struct slash *slash,
				    const struct slash_command *cmd)
{
	while (cmd) {
		if (cmd->flags & SLASH_FLAG_HIDDEN)
			return true;

		if (!slash->privileged && (cmd->flags & SLASH_FLAG_PRIVILEGED))
			return true;

		/* Check that parent command is also not hidden */
		cmd = cmd->parent;
	}

	return false;
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
	struct slash_command *cur, *command = NULL;
	char *next = line, *token;
	size_t tokenlen;
	bool found;

	while ((token = slash_command_line_token(next, &tokenlen, &next))) {
		if (token >= line + linelen)
			break;

		found = false;

		slash_command_list_for_each(cur) {
			/* Skip if parent does not match */
			if (cur->parent != command)
				continue;

			/* Skip if name does not match */
			if (strncmp(token, cur->name, tokenlen) != 0)
				continue;

			/* Ensure entire command name matches */
			if (strlen(cur->name) != tokenlen)
				continue;

			/* Skip if privileged command in non-privileged mode */
			if (!slash->privileged &&
			    (cur->flags & SLASH_FLAG_PRIVILEGED))
				continue;

			/* We found our command */
			command = cur;
			*args = token;
			found = true;
		}

		if (!found)
			break;
	}

	return command;
}

static int slash_build_args(struct slash *slash, char *args)
{
	/* Quote level */
	enum {
		SLASH_QUOTE_NONE,
		SLASH_QUOTE_SINGLE,
		SLASH_QUOTE_DOUBLE,
	} quote = SLASH_QUOTE_NONE;

	slash->argc = 0;

	while (*args && slash->argc < SLASH_ARG_MAX) {
		/* Check for quotes */
		if (*args == '\'') {
			quote = SLASH_QUOTE_SINGLE;
			args++;
		} else if (*args == '\"') {
			quote = SLASH_QUOTE_DOUBLE;
			args++;
		}

		/* Argument starts here */
		slash->argv[slash->argc++] = args;

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
		return -EINVAL;

	/* If args does not point to NULL, we ran out of argv space */
	if (*args)
		return -E2BIG;

	/* According to C11 section 5.1.2.2.1, argv[argc] must be NULL */
	slash->argv[slash->argc] = NULL;

	return 0;
}

static void slash_command_name_recurse(struct slash *slash, struct slash_command *command)
{
	if (command->parent) {
		slash_command_name_recurse(slash, command->parent);
		slash_printf(slash, " ");
	}
	slash_printf(slash, "%s", command->name);
}

static void slash_command_usage(struct slash *slash, struct slash_command *command)
{
	const char *args = command->args ? command->args : "";
	const char *type = command->func ? "usage" : "group";

	slash_printf(slash, "%s: ", type);
	slash_command_name_recurse(slash, command);
	slash_printf(slash, " %s\n", args);
}

static void slash_command_description(struct slash *slash, struct slash_command *command)
{
	char *nl;
	const char *help = "";
	size_t desclen = 0;

	/* Extract first line from help as description */
	if (command->help != NULL) {
		help = command->help;
		nl = strchr(help, '\n');
		desclen = nl ? (size_t)(nl - help) : strlen(help);
	}

	slash_printf(slash, "%-15s %.*s\n", command->name, desclen, help);
}

static void slash_command_help(struct slash *slash, struct slash_command *command)
{
	size_t slen;
	const char *help = "";
	struct slash_command *cur;
	bool first = true;

	if (command->help != NULL)
		help = command->help;

	slash_command_usage(slash, command);
	slash_printf(slash, "\n%s", help);

	slen = strlen(help);
	if (slen > 0 && help[slen - 1] != '\n')
		slash_printf(slash, "\n");

	slash_command_list_for_each(cur) {
		if (cur->parent == command) {
			if (first) {
				slash_printf(slash,
					     "\nAvailable subcommands in \'%s\' group:\n",
					     command->name);
				first = false;
			}
			slash_command_description(slash, cur);
		}
	}
}

int slash_execute(struct slash *slash, char *line)
{
	struct slash_command *command, *cur;
	char *args;
	int ret;

	/* Fast path for empty lines or comments */
	if (slash_line_empty_or_comment(line, strlen(line)))
		return 0;

	command = slash_command_find(slash, line, strlen(line), &args);
	if (!command) {
		slash_printf(slash, "No such command: %s\n", line);
		return -ENOENT;
	}

	if (!command->func) {
		slash_printf(slash, "Available subcommands in \'%s\' group:\n",
			     command->name);
		slash_command_list_for_each(cur) {
			if (cur->parent == command)
				slash_command_description(slash, cur);
		}
		return -EISDIR;
	}

	/* Build args */
	ret = slash_build_args(slash, args);
	if (ret < 0) {
		if (ret == -EINVAL)
			slash_printf(slash, "Mismatched quotes\n");
		else if (ret == -E2BIG)
			slash_printf(slash, "Too many arguments\n");
		else
			slash_printf(slash, "Invalid arguments\n");
		return ret;
	}

	/* Reset state for slash_getopt */
	slash->optarg = 0;
	slash->optind = 1;
	slash->opterr = 1;
	slash->optopt = '?';
	slash->sp = 1;

	/* Set command context */
	slash->context = command->context;

	ret = command->func(slash);

	if (ret == SLASH_EUSAGE)
		slash_command_usage(slash, command);
	else if (ret == SLASH_EHELP)
		slash_command_help(slash, command);

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

static bool slash_complete_confirm(struct slash *slash, int matches)
{
	char c = 'y';

	if (matches <= SLASH_SHOW_MAX)
		return true;

	slash_printf(slash, "Display all %d possibilities? (y or n) ", matches);
	do {
		if (c != 'y')
			slash_bell(slash);
		c = slash_getchar(slash);
	} while (c != 'y' && c != 'n' && c != '\t' &&
		(isprint((int)c) || isspace((int)c)));

	slash_printf(slash, "\n");

	return (c == 'y' || c == '\t');
}

static int slash_prefix_length(const char *s1, const char *s2)
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
				 char *complete, const char *match,
				 int len, bool space)
{
	strncpy(complete, match, len);
	complete[len] = '\0';
	if (space)
		strncat(complete, " ", slash->line_size - 1);
	slash->length = strlen(slash->buffer);
	slash_mark_changed(slash, slash->cursor, slash->length);
	slash->cursor = slash->length;
}

static bool slash_complete_matches(struct slash *slash,
				   struct slash_command *command,
				   struct slash_command *cur,
				   char *complete, size_t completelen)
{
	if (cur->parent != command)
		return false;

	if (slash_command_is_hidden(slash, cur))
		return false;

	if (strncmp(cur->name, complete, completelen) != 0)
		return false;

	return true;
}

static void slash_complete(struct slash *slash)
{
	size_t completelen = 0, commandlen = 0, prefixlen = 0, matches;
	char *complete, *args;
	struct slash_command *cur, *command = NULL, *prefix = NULL;

	/* Find start of word to complete */
	complete = slash_last_word(slash->buffer, slash->cursor, &completelen);
	commandlen = complete - slash->buffer;

	/* Determine if we are completing sub command */
	if (!slash_line_empty(slash->buffer, commandlen)) {
		command = slash_command_find(slash, slash->buffer, commandlen, &args);
		if (!command || slash_command_is_hidden(slash, command))
			return;
	}

	/* Search list for matches */
	matches = 0;
	slash_command_list_for_each(cur) {
		if (!slash_complete_matches(slash, command, cur,
					    complete, completelen))
			continue;

		matches++;

		/* Find common prefix */
		if (!prefix) {
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
			slash->refresh_full = true;
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
		if (slash_complete_confirm(slash, matches)) {
			/* List matches */
			slash_command_list_for_each(cur) {
				if (!slash_complete_matches(slash, command, cur,
							    complete, completelen))
					continue;

				slash_command_description(slash, cur);
			}
		}
		slash->refresh_full = true;
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

static bool slash_history_next(struct slash *slash)
{
	char *src;
	size_t srclen;

	src = slash_history_search_forward(slash, slash->history_cursor, &srclen);
	if (!src)
		return false;

	slash->history_depth--;
	slash_history_copy(slash, slash->buffer, src, srclen);
	slash->buffer[srclen] = '\0';
	slash->history_cursor = src;
	slash->cursor = slash->length = srclen;
	slash_mark_changed(slash, 0, slash->length);

	/* Rewind if use to store buffer temporarily */
	if (!slash->history_depth && slash->history_cursor != slash->history_tail)
		slash_history_rewind(slash, slash->history_rewind_length);

	return true;
}

static bool slash_history_previous(struct slash *slash)
{
	char *src;
	size_t srclen, buflen;

	src = slash_history_search_back(slash, slash->history_cursor, &srclen);
	if (!src)
		return false;

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
	slash_mark_changed(slash, 0, slash->length);

	return true;
}

/* Line editing */
static int slash_screen_cursor_back(struct slash *slash, size_t n)
{
	/* If we need to move more than 3 colums, CUB uses fewer bytes */
	if (n > 3) {
		slash_printf(slash, ESCAPE("%zuD"), n);
		slash->cursor_screen -= n;
	} else {
		while (n--) {
			slash_putchar(slash, '\b');
			slash->cursor_screen--;
		}
	}

	return 0;
}

static int slash_screen_cursor_forward(struct slash *slash, size_t n)
{
	/* If we need to move more than 3 colums, CUF uses fewer bytes */
	if (n > 3) {
		slash_printf(slash, ESCAPE("%zuC"), n);
		slash->cursor_screen += n;
	} else {
		while (n--) {
			slash_putchar(slash, slash->buffer[slash->cursor_screen]);
			slash->cursor_screen++;
		}
	}

	return 0;
}

static int slash_screen_cursor_to_column(struct slash *slash, size_t col)
{
	size_t diff;

	if (col > slash->cursor_screen) {
		diff = col - slash->cursor_screen;
		return slash_screen_cursor_forward(slash, diff);
	} else if (col < slash->cursor_screen) {
		diff = slash->cursor_screen - col;
		return slash_screen_cursor_back(slash, diff);
	}

	return 0;
}

int slash_refresh(struct slash *slash)
{
	const char *esc = ESCAPE("K");

	/* Full refresh with prompt */
	if (slash->refresh_full) {
		slash_putchar(slash, '\r');
		if (slash_write(slash, esc, strlen(esc)) < 0)
			return -1;
		if (slash_write(slash, slash->prompt, slash->prompt_length) < 0)
			return -1;
		slash->cursor_screen = 0;
		slash->length_screen = 0;
		slash->change_start = 0;
		slash->change_end = slash->length;
		slash->refresh_full = false;
	}

	/* Buffer contents have changed */
	if (slash->change_start != slash->change_end) {
		if (slash_screen_cursor_to_column(slash, slash->change_start) < 0)
			return -1;
		if (slash_write(slash,
				&slash->buffer[slash->change_start],
				slash->change_end - slash->change_start) < 0)
			return -1;
		slash->cursor_screen = slash->change_end;
		slash->change_start = slash->change_end = 0;
	}

	/* If screen contents were truncated, erase remainder */
	if (slash->length_screen > slash->length) {
		if (slash_screen_cursor_to_column(slash, slash->length) < 0)
			return -1;
		if (slash_write(slash, esc, strlen(esc)) < 0)
			return -1;
	}
	slash->length_screen = slash->length;

	/* Restore screen cursor position */
	if (slash_screen_cursor_to_column(slash, slash->cursor) < 0)
		return -1;

	return slash_write_flush(slash);
}

static void slash_insert(struct slash *slash, int c)
{
	if (slash->length >= slash->line_size)
		return;

	memmove(&slash->buffer[slash->cursor + 1],
		&slash->buffer[slash->cursor],
		slash->length - slash->cursor);
	slash->buffer[slash->cursor] = c;
	slash->length++;
	slash_mark_changed(slash, slash->cursor, slash->length);
	slash->cursor++;
	slash->buffer[slash->length] = '\0';
}

static void slash_delete(struct slash *slash)
{
	if (slash->cursor >= slash->length)
		return;

	slash->length--;
	memmove(&slash->buffer[slash->cursor],
		&slash->buffer[slash->cursor + 1],
		slash->length - slash->cursor);
	slash_mark_changed(slash, slash->cursor, slash->length);
	slash->buffer[slash->length] = '\0';
}

void slash_reset(struct slash *slash)
{
	slash->buffer[0] = '\0';
	slash->length = 0;
	slash->cursor = 0;
	slash->change_start = 0;
	slash->change_end = 0;
	slash->refresh_full = true;
}

static void slash_arrow_up(struct slash *slash)
{
	if (!slash_history_previous(slash))
		slash_bell(slash);
}

static void slash_arrow_down(struct slash *slash)
{
	if (!slash_history_next(slash))
		slash_bell(slash);
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

void slash_clear_screen(struct slash *slash)
{
	const char *esc = ESCAPE("H") ESCAPE("2J");
	slash_write(slash, esc, strlen(esc));
	slash->refresh_full = true;
}

static void slash_backspace(struct slash *slash)
{
	if (slash->cursor == 0)
		return;

	slash->cursor--;
	slash->length--;
	memmove(&slash->buffer[slash->cursor],
		&slash->buffer[slash->cursor + 1],
		slash->length - slash->cursor);
	slash_mark_changed(slash, slash->cursor, slash->length);
	slash->buffer[slash->length] = '\0';
}

static void slash_delete_word(struct slash *slash)
{
	size_t old_cursor = slash->cursor;

	while (slash->cursor > 0 && slash->buffer[slash->cursor-1] == ' ')
		slash->cursor--;
	while (slash->cursor > 0 && slash->buffer[slash->cursor-1] != ' ')
		slash->cursor--;

	slash->length -= old_cursor - slash->cursor;
	memmove(&slash->buffer[slash->cursor],
		&slash->buffer[old_cursor],
		slash->length - slash->cursor);
	slash_mark_changed(slash, slash->cursor, slash->length);
	slash->buffer[slash->length] = '\0';
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

void slash_set_prompt(struct slash *slash, const char *prompt)
{
	slash->prompt = prompt;
	slash->prompt_length = strlen(prompt);
}

char *slash_readline(struct slash *slash)
{
	char *ret = slash->buffer;
	int c, esc[3];
	bool done = false, escaped = false;

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
#ifndef SLASH_NO_EXIT
					if (!slash->exit_inhibit)
						ret = NULL;
#endif
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
				slash->buffer[slash->length] = '\0';
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
				slash->buffer[0] = '\0';
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

		slash_refresh(slash);

		slash->last_char = c;
	}

	slash_putchar(slash, '\n');
	slash_history_add(slash, slash->buffer);

	return ret;
}

/* Builtin commands */
static int slash_builtin_help(struct slash *slash)
{
	int i;
	char *args;
	struct slash_command *cur;

	/* If no arguments given, just list all top-level commands */
	if (slash->argc < 2) {
		slash_printf(slash, "Available commands:\n");
		slash_command_list_for_each(cur) {
			if (cur->parent)
				continue;
			if (slash_command_is_hidden(slash, cur))
				continue;
			slash_command_description(slash, cur);
		}
		return SLASH_SUCCESS;
	}

	/* Unbuild args */
	for (i = 2; i < slash->argc; i++)
		*(slash->argv[i] - 1) = ' ';

	cur = slash_command_find(slash, slash->argv[1], strlen(slash->argv[1]), &args);
	if (!cur) {
		slash_printf(slash, "No such command: %s\n", slash->argv[1]);
		return SLASH_EINVAL;
	}

	slash_command_help(slash, cur);

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

static int slash_builtin_echo(struct slash *slash)
{
	int i;

	for (i = 1; i < slash->argc; i++)
		printf("%s ", slash->argv[i]);

	printf("\n");

	return SLASH_SUCCESS;
}
slash_command(echo, slash_builtin_echo, "[string]",
	      "Display a line of text");

#ifndef SLASH_NO_EXIT
static int slash_builtin_exit(struct slash *slash)
{
	if (slash->exit_inhibit) {
		slash_printf(slash, "Exit has been disabled in this console\n");
		return 0;
	}

	return SLASH_EXIT;
}
slash_command(exit, slash_builtin_exit, NULL,
	      "Exit application");
#endif

void slash_require_activation(struct slash *slash, bool activate)
{
	slash->use_activate = activate;
}

void slash_inhibit_exit(struct slash *slash, bool inhibit)
{
	slash->exit_inhibit = inhibit;
}

/* Core */
int slash_loop(struct slash *slash)
{
	int c, ret;
	char *line;

	if (slash_configure_term(slash) < 0)
		return -ENOTTY;

	if (slash->use_activate) {
		slash_printf(slash, "Press enter to activate this console ");
		do {
			c = slash_getchar(slash);
		} while (c != '\n' && c != '\r');
	}

	while ((line = slash_readline(slash))) {
		ret = slash_execute(slash, line);
		if (ret == SLASH_EXIT)
			break;
	}

	slash_restore_term(slash);

	return 0;
}

int slash_init(struct slash *slash,
	       char *line, size_t line_size,
	       char *history, size_t history_size)
{
	/* Ensure context and buffers are zero */
	memset(slash, 0, sizeof(*slash));
	memset(line, 0, line_size);
	memset(history, 0, history_size);

	/* Setup default values */
	slash->file_read = stdin;
	slash->file_write = stdout;
#ifdef SLASH_HAVE_SELECT
	slash->waitfunc = slash_wait_select;
#endif

	/* Set default prompt */
	slash_set_prompt(slash, "slash> ");

	/* Initialize line buffer */
	slash->buffer = line;
	slash->line_size = line_size;

	/* Initialize history */
	slash->history = history;
	slash->history_size = history_size;
	slash->history_head = slash->history;
	slash->history_tail = slash->history;
	slash->history_cursor = slash->history;
	slash->history_avail = slash->history_size - 1;

	return 0;
}

struct slash *slash_create(size_t line_size, size_t history_size)
{
	struct slash *slash = NULL;
	char *line = NULL, *history = NULL;

	/* Allocate slash context and buffers */
	slash = malloc(sizeof(*slash));
	line = malloc(line_size);
	history = malloc(history_size);

	if (slash && line && history) {
		if (!slash_init(slash, line, line_size, history, history_size))
			return slash;
	}

	/* Allocation or initialization error */
	free(slash);
	free(line);
	free(history);

	return NULL;
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

	free(slash);
}
