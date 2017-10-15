/*
 * Copyright (c) 2016 Max Fillinger
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <check.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "comm.h"
#include "pnp.h"

START_TEST (child_exits_on_CMD_EXIT)
{
	pid_t		child;
	int		sv[2];
	struct out	out;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child = fork();
	switch (child) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		out.type = OUT_RAW;
		out.handle.fp = NULL;
		if (child_main(sv, &out) == -1)
			ck_abort_msg("child_main failed.");
	default:
		/* Parent process */
		parent_init(sv, child);
		stop_child();
		sleep(1);
		ck_assert_int_eq(check_child(), 0);
	}
}
END_TEST

START_TEST (parent_handles_child_exit)
{
	pid_t		child;
	int		sv[2];
	struct imsg	msg;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child = fork();
	switch (child) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		_exit(1);
	default:
		/* Parent process */
		parent_init(sv, child);
		while (1) {
			if (parent_process_events(&msg) > 0)
				imsg_free(&msg);
		}
	}
}
END_TEST

START_TEST (parent_ignores_false_SIGCHLD)
{
	pid_t		child;
	int		sv[2];
	struct imsg	msg;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child= fork();
	switch (child) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		kill(getppid(), SIGCHLD);
		while (1)
			;
	default:
		/* Parent process */
		parent_init(sv, child);
		parent_process_events(&msg);
		ck_assert_int_eq(check_child(), 1);
	}
}
END_TEST

START_TEST (send_new_file_returns_0_on_valid_file)
{
	pid_t		child;
	int		sv[2], rv;
	struct imsg	msg;
	struct out	out;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child = fork();
	switch (child) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		out.type = OUT_RAW;
		out.handle.fp = NULL;
		rv = child_main(sv, &out);
		if (rv == -1)
			ck_abort_msg("child_main failed.");
	default:
		/* Parent process */
		parent_init(sv, child);
		parent_process_events(&msg);
		rv = send_new_file("testdata/test.flac");
		ck_assert_int_eq(rv, 0);
	}
}
END_TEST

START_TEST (send_new_file_returns_1_on_invalid_file)
{
	struct imsg	msg;
	struct out	out;
	pid_t		child;
	int		sv[2], rv;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child= fork();
	switch (child) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		out.type = OUT_RAW;
		out.handle.fp = NULL;
		rv = child_main(sv, &out);
		if (rv == -1)
			ck_abort_msg("child_main failed.");
	default:
		/* Parent process */
		parent_init(sv, child);
		parent_process_events(&msg);
		rv = send_new_file("testdata/random_garbage");
		ck_assert_int_eq(rv, 1);
	}
}
END_TEST

void
test_err_cb(int type, char *msg)
{
	if (type == PNP_CHILD_FATAL) {
		ck_assert(1 == 1);
		exit(0);
	}
}

Suite
*ipc_suite(void)
{
	Suite *s;
	TCase *tc_cmd, *tc_signals, *tc_err;

	s = suite_create("Interprocess Communication");
	tc_cmd = tcase_create("Commands");
	tc_signals = tcase_create("Signal handling");
	tc_err = tcase_create("Error handling");

	tcase_add_test(tc_cmd, child_exits_on_CMD_EXIT);
	tcase_add_test(tc_cmd, send_new_file_returns_0_on_valid_file);
	tcase_add_test(tc_cmd, send_new_file_returns_1_on_invalid_file);
	tcase_add_exit_test(tc_signals, parent_handles_child_exit, 1);
	tcase_add_test(tc_signals, parent_ignores_false_SIGCHLD);
	suite_add_tcase(s, tc_cmd);
	suite_add_tcase(s, tc_signals);
	suite_add_tcase(s, tc_err);
	
	return (s);
}

int
main(void)
{
	int	no_failed;
	Suite	*s;
	SRunner	*sr;

	s = ipc_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	no_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return ((no_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}
