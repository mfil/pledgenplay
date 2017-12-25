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
#include "message_types.h"
#include "mock_errors.h"

/* Mock up inter-process communication. */

static struct imsgbuf ibuf;

static void prepare_mock_ipc(void);
static void parent_sends_message(MESSAGE_TYPE);
static void parent_sends_new_input_file(char []);

static void
prepare_mock_ipc()
{
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sockets) == -1) {
		err(1, "socketpair");
	}

	/* "Parent" */
	imsg_init(&ibuf, sockets[0]);

	/* "Child" */
	initialize_ipc(sockets[1]);
}

static void
parent_sends_message(MESSAGE_TYPE type)
{
	if (imsg_compose(&ibuf, (uint32_t)type, 0, getpid(), -1, NULL, 0) ==
	    -1) {
		err(1, "imsg_compose");
	}
	if (imsg_flush(&ibuf) == -1) {
		err(1, "imsg_flush");
	}
}

static void
parent_sends_new_input_file(char filename[])
{
	int fd = open(filename, O_RDONLY);
	if (fd == -1) {
		err(1, "open");
	}
	if (imsg_compose(&ibuf, (uint32_t)CMD_SET_INPUT, 0, getpid(), fd,
	    NULL, 0) == -1) {
		err(1, "imsg_compose");
	}
	if (imsg_flush(&ibuf) == -1) {
		err(1, "imsg_flush");
	}
}

START_TEST (get_next_message_returns_NO_MESSAGES_when_no_message_ready)
{
	prepare_mock_ipc();

	/* "Child" tries to get a message. */

	struct message message;
	GET_NEXT_MSG_STATUS status = get_next_message(&message);
	ck_assert_int_eq(status, NO_MESSAGES);
}
END_TEST

START_TEST (get_next_message_receives_command_messages)
{
	prepare_mock_ipc();

	/* _i loops over all valid message types (without associated files). */

	parent_sends_message(_i);

	/* "Child" tries to get the message. */

	check_for_messages();
	struct message message;
	GET_NEXT_MSG_STATUS status = get_next_message(&message);
	ck_assert_int_eq(status, GOT_MESSAGE);
	ck_assert_int_eq(message.type, _i);
}
END_TEST

START_TEST (get_next_message_receives_input_file)
{
	prepare_mock_ipc();
	parent_sends_new_input_file("./testdata/test.flac");

	/* Check if the message has arrived and has the correct type. */

	check_for_messages();
	struct message message;
	GET_NEXT_MSG_STATUS status = get_next_message(&message);
	ck_assert_int_eq(status, GOT_MESSAGE);
	ck_assert_int_eq(message.type, CMD_SET_INPUT);

	/*
	 * Check if the received fd can be read. It should start with the magic
	 * bytes "fLaC".
	 */
	int received_fd = message.data.fd;
	char magic_bytes[5] = "fLaC";
	char read_bytes[5] = { 0, 0, 0, 0, 0 };
	read(received_fd, read_bytes, 4);
	ck_assert_str_eq(read_bytes, magic_bytes);
	close(received_fd);
}
END_TEST

START_TEST (get_next_message_raises_fatal_error_on_invalid_message_type)
{
	prepare_mock_ipc();

	parent_sends_message(CMD_MESSAGE_SENTINEL);

	/* "Child" gets message. */

	check_for_messages();
	struct message message;
	(void)get_next_message(&message);
}
END_TEST

START_TEST (enqueue_message_raises_fatal_error_for_invalid_type)
{
	prepare_mock_ipc();
	enqueue_message(MSG_SENTINEL, NULL);
}
END_TEST

START_TEST (enqueue_message_warns_about_over_long_messages)
{
	prepare_mock_ipc();

	/* Prepare an over-long message. */

	char long_message[UINT16_MAX+1];
	memset(long_message, 'A', sizeof(long_message));
	long_message[UINT16_MAX] = '\0';

	/* Check if child_warn() is called. */

	child_warn_called = 0;
	enqueue_message(MSG_ACK, long_message);
	ck_assert_int_ne(child_warn_called, 0);
}
END_TEST

START_TEST (child_can_send_messages)
{
	prepare_mock_ipc();

	/* _i loops over valid MESSAGE_TYPEs. */

	const char test_message[] = "Test message";
	enqueue_message(_i, test_message);
	send_messages();

	/* Parent receives messages. */

	if (imsg_read(&ibuf) <= 0) {
		err(1, "imsg_read");
	}
	struct imsg imessage;
	if (imsg_get(&ibuf, &imessage) <= 0) {
		err(1, "imsg_get");
	}

	ck_assert_int_eq((int)imessage.hdr.type, _i);
	ck_assert_str_eq((char *)imessage.data, test_message);
	imsg_free(&imessage);
}
END_TEST

START_TEST (child_can_send_multiple_messages)
{
	prepare_mock_ipc();

	const char test_message[] = "Test message";

	/* Some arbitrary message types. */

	const MESSAGE_TYPE types[] = { MSG_WARN, MSG_FATAL, META_ARTIST };
	const size_t types_count = sizeof(types)/sizeof(types[0]);
	int i;
	for (i = 0; i < types_count; i++) {
		enqueue_message(types[i], test_message);
	}
	send_messages();

	/* Parent receives messages. */

	int j;
	if (imsg_read(&ibuf) <= 0) {
		err(1, "imsg_read");
	}
	for (j = 0; j < types_count; j++) {
		struct imsg imessage;
		if (imsg_get(&ibuf, &imessage) <= 0) {
			err(1, "imsg_get");
		}
		ck_assert_int_eq((int)imessage.hdr.type, types[j]);
		ck_assert_str_eq((char *)imessage.data, test_message);
		imsg_free(&imessage);
	}
}
END_TEST

Suite
*child_messages_suite(void)
{
	Suite *s = suite_create("Test child_messages");

	TCase *tc_get_next_message = tcase_create("get_next_message");
	tcase_add_test(tc_get_next_message,
	    get_next_message_returns_NO_MESSAGES_when_no_message_ready);
	tcase_add_loop_test(tc_get_next_message,
	    get_next_message_receives_command_messages,
	    1, CMD_MESSAGE_SENTINEL);
	tcase_add_test(tc_get_next_message,
	    get_next_message_receives_input_file);
	tcase_add_exit_test(tc_get_next_message,
	    get_next_message_raises_fatal_error_on_invalid_message_type,
	    FATAL_EXIT_CODE);
	suite_add_tcase(s, tc_get_next_message);

	TCase *tc_enqueue_message = tcase_create("enqueue_message");
	tcase_add_exit_test(tc_enqueue_message,
	    enqueue_message_raises_fatal_error_for_invalid_type,
	    FATAL_EXIT_CODE);
	tcase_add_test(tc_enqueue_message,
	    enqueue_message_warns_about_over_long_messages);
	tcase_add_loop_test(tc_enqueue_message, child_can_send_messages,
	    0, MSG_SENTINEL);
	tcase_add_test(tc_enqueue_message, child_can_send_multiple_messages);
	suite_add_tcase(s, tc_enqueue_message);
	
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
