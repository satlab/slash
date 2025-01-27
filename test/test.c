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

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <setjmp.h>
#include <cmocka.h>

#include <string.h>

#include <slash/slash.h>

#define LINE_SIZE	128
#define HISTORY_SIZE	128

static int cmd_test(struct slash *slash)
{
	if (slash->argc != 2 ||
	    strcmp(slash->argv[0], "test") ||
	    strcmp(slash->argv[1], "arg") || 
	    slash->argv[slash->argc] != NULL)
		return SLASH_EINVAL;

	/* C requirement */
	if (slash->argv[slash->argc] != NULL)
		return SLASH_EINVAL;

	/* Context should be NULL unles set */
	if (slash->context != NULL)
		return SLASH_EINVAL;

	return SLASH_SUCCESS;
}
slash_command(test, cmd_test, NULL, NULL);

static int cmd_test_sub(struct slash *slash)
{
	if (slash->argc != 1)
		return SLASH_EUSAGE;

	return SLASH_SUCCESS;
}
slash_command_sub(test, sub, cmd_test_sub, NULL, NULL);

static int cmd_test_subsub(struct slash *slash)
{
	if (slash->argc != 1)
		return SLASH_EUSAGE;

	return SLASH_SUCCESS;
}
slash_command_subsub(test, sub, subsub, cmd_test_subsub, NULL, NULL);

static int cmd_test_subsubsub(struct slash *slash)
{
	if (slash->argc != 1)
		return SLASH_EUSAGE;

	return SLASH_SUCCESS;
}
slash_command_subsubsub(test, sub, subsub, subsubsub,
			cmd_test_subsubsub, NULL, NULL);

slash_command_group(group, NULL);
slash_command_subgroup(group, subgroup, NULL);
slash_command_subsubgroup(group, subgroup, subsubgroup, NULL);
slash_command_subsubsub(group, subgroup, subsubgroup, subsubsub,
			cmd_test_subsubsub, NULL, NULL);

static int cmd_privileged(struct slash *slash)
{
	return SLASH_SUCCESS;
}
slash_command(privileged, cmd_privileged, NULL, NULL,
	      SLASH_FLAG_PRIVILEGED);

static int cmd_context(struct slash *slash)
{
	if (slash->context != (void *)123)
		return SLASH_EINVAL;

	return SLASH_SUCCESS;
}
slash_command(context, cmd_context, NULL, NULL,
	      0, (void *)123);

static void slash_test_command(void **state)
{
	struct slash *slash = *state;

	int ret;
	char *cmd = "test arg";

	ret = slash_execute(slash, cmd);
	assert_int_equal(ret, 0);
}

static void slash_test_sub_command(void **state)
{
	struct slash *slash = *state;

	int ret;
	char *cmd = "test sub";

	ret = slash_execute(slash, cmd);
	assert_int_equal(ret, 0);
}

static void slash_test_subsub_command(void **state)
{
	struct slash *slash = *state;

	int ret;
	char *cmd = "test sub subsub";

	ret = slash_execute(slash, cmd);
	assert_int_equal(ret, 0);
}

static void slash_test_subsubsub_command(void **state)
{
	struct slash *slash = *state;

	int ret;
	char *cmd = "test sub subsub subsubsub";

	ret = slash_execute(slash, cmd);
	assert_int_equal(ret, 0);
}

static void slash_test_subsubsub_command_in_group(void **state)
{
	struct slash *slash = *state;

	int ret;
	char *cmd = "group subgroup subsubgroup subsubsub";

	ret = slash_execute(slash, cmd);
	assert_int_equal(ret, 0);
}

static void slash_test_privileged_command(void **state)
{
	struct slash *slash = *state;

	int ret;
	char *cmd = "privileged";

	ret = slash_execute(slash, cmd);
	assert_int_equal(ret, -ENOENT);

	slash_set_privileged(slash, true);
	ret = slash_execute(slash, cmd);
	assert_int_equal(ret, 0);

	slash_set_privileged(slash, false);
}

static void slash_test_context_command(void **state)
{
	struct slash *slash = *state;

	int ret;
	char *cmd = "context";

	ret = slash_execute(slash, cmd);
	assert_int_equal(ret, 0);
}

static void slash_test_partial(void **state)
{
	struct slash *slash = *state;

	int ret;
	char *cmd = "e"; /* Partial match on builtin echo */

	ret = slash_execute(slash, cmd);
	assert_int_equal(ret, -ENOENT);
}

static int setup(void **state)
{
	struct slash *slash = slash_create(LINE_SIZE, HISTORY_SIZE);
	if (!slash)
		return -1;
	
	*state = slash;
	return 0;
}

static int teardown(void **state)
{
	struct slash *slash = *state;
	slash_destroy(slash);
	return 0;
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(slash_test_command),
		cmocka_unit_test(slash_test_sub_command),
		cmocka_unit_test(slash_test_subsub_command),
		cmocka_unit_test(slash_test_subsubsub_command),
		cmocka_unit_test(slash_test_subsubsub_command_in_group),
		cmocka_unit_test(slash_test_privileged_command),
		cmocka_unit_test(slash_test_context_command),
		cmocka_unit_test(slash_test_partial),
	};

	return cmocka_run_group_tests(tests, setup, teardown);
}
