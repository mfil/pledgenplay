/*
 * Copyright (c) 2017 Max Fillinger
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

#include <check.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

const char *child_main_path = "../obj/pnp_child";

START_TEST (child_main_exits_if_no_filedescriptor_is_given)
{
	execl(child_main_path, "pnp_child", NULL);
}
END_TEST

START_TEST (child_main_exits_if_a_bogus_file_descriptor_is_given)
{
	const char *bad_fds[] = {"-1", "23", "100"};
	execl(child_main_path, "pnp_child", bad_fds[_i], NULL);
}
END_TEST

START_TEST (child_main_exits_if_the_fd_is_not_a_socket)
{
	int fd = open("testdata/test.flac", O_RDONLY);
	char *fdstr;
	asprintf(&fdstr, "%d", fd);
	if (fdstr == NULL) {
		err(2, "asprintf");
	}
	execl(child_main_path, "pnp_child", fdstr, NULL);
}
END_TEST

Suite
*test_suite(void)
{
	Suite *s;
	TCase *tc_init;

	s = suite_create("child main");
	tc_init = tcase_create("Initialization");

	tcase_add_exit_test(tc_init,
	    child_main_exits_if_no_filedescriptor_is_given, 1);
	tcase_add_loop_exit_test(tc_init,
	    child_main_exits_if_a_bogus_file_descriptor_is_given, 1, 0, 3);
	tcase_add_exit_test(tc_init, child_main_exits_if_the_fd_is_not_a_socket,
	    1);

	suite_add_tcase(s, tc_init);
	
	return (s);
}

int
main(void)
{
	int	no_failed;
	Suite	*s;
	SRunner	*sr;

	s = test_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	no_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return ((no_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}
