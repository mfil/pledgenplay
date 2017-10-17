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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <err.h>
#include <fcntl.h>
#include <imsg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "child_messages.h"

/* Mock error functions. */

static int child_warn_called = 0;
static int file_err_called = 0;

void
ipc_error(const char *message) {
	exit(1);
}

void
child_fatal(const char *message) {
	exit(1);
}

void
child_fatalx(const char *message) {
	exit(1);
}

void
child_warn(const char *message) {
	child_warn_called = 1;
}

void
child_warnx(const char *message) {
	child_warn_called = 1;
}

void
file_err(const char *message) {
	file_err_called = 1;
}

void
file_errx(const char *message) {
	file_err_called = 1;
}

START_TEST (get_next_message_returns_0_when_no_message_ready)
{
	/* Prepare communication sockets. */

	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");

	/* Prepare ibuf. */

	struct imsgbuf	ibuf;
	imsg_init(&ibuf, sv[0]);

	/* Initialize message handling of child. */

	initialize_ipc(sv[1]);

	/* Try to get a message. */

	struct message message;
	ck_assert_int_eq(get_next_message(&message), 0);
}
END_TEST

START_TEST (get_next_message_receives_command_messages)
{
	/* Prepare communication sockets. */

	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");

	/* Prepare ibuf. */

	struct imsgbuf	ibuf;
	imsg_init(&ibuf, sv[0]);

	/* Initialize message handling of child. */

	initialize_ipc(sv[1]);

	/* Send the message. */

	MESSAGE_TYPE command_message_types[4] = { CMD_EXIT, CMD_META, CMD_PLAY,
	    CMD_PAUSE };
	imsg_compose(&ibuf, (uint32_t)command_message_types[_i], 0, getpid(),
	    -1, NULL, 0);
	imsg_flush(&ibuf);

	/* Try to get a message. */

	receive_messages();
	struct message message;
	int status = get_next_message(&message);
	ck_assert_int_eq(status, 1);
	ck_assert_int_eq(message.type, command_message_types[_i]);
}
END_TEST

START_TEST (get_next_message_receives_fd)
{
	/* Prepare communication sockets. */

	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");

	/* Prepare ibuf. */

	struct imsgbuf	ibuf;
	imsg_init(&ibuf, sv[0]);

	/* Initialize message handling of child. */

	initialize_ipc(sv[1]);

	/* Send the message. */

	int fd = open("./testdata/test.flac", O_RDONLY);
	imsg_compose(&ibuf, (uint32_t)NEW_FILE, 0, getpid(), fd, NULL, 0);
	imsg_flush(&ibuf);

	/* Try to get the message. */

	receive_messages();
	struct message message;
	int status = get_next_message(&message);

	/* Check if the message has arrived and has the correct type. */

	ck_assert_int_eq(status, 1);
	ck_assert_int_eq(message.type, NEW_FILE);

	/*
	 * Check if the received fd can be read. It should start with the magic
	 * bytes 'fLaC'.
	 */
	int received_fd = message.data.fd;
	char magic_bytes[5] = "fLaC";
	char read_bytes[5] = { 0, 0, 0, 0, 0 };
	read(received_fd, read_bytes, 4);
	ck_assert_str_eq(read_bytes, magic_bytes);
	close(received_fd);
}
END_TEST

Suite
*child_messages_suite(void)
{
	Suite *s = suite_create("Test child_messages");
	TCase *tc_get_next_message = tcase_create("get_next_message");
	tcase_add_test(tc_get_next_message,
	    get_next_message_returns_0_when_no_message_ready);
	tcase_add_loop_test(tc_get_next_message,
	    get_next_message_receives_command_messages, 0, 4);
	tcase_add_test(tc_get_next_message, get_next_message_receives_fd);

	suite_add_tcase(s, tc_get_next_message);
	
	return (s);
}

int
main(void)
{
	int	no_failed;
	Suite	*s;
	SRunner	*sr;

	s = child_messages_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	no_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return ((no_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}