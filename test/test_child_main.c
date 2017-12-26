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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../message_types.h"

const char *const pnp_child_path = "../obj/pnp_child";

int
open_input_file(const char *filename)
{
	int in_fd = open(filename, O_RDONLY);
	if (in_fd == -1) {
		err(1, "open");
	}
	return (in_fd);
}

int
open_output_file(const char *filename)
{
	int out_fd = open("scratchspace/test.raw",
	    O_WRONLY | O_CREAT | O_TRUNC,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (out_fd == -1) {
		err(1, "open");
	}
	return (out_fd);
}

void
start_pnp_child(pid_t *child_pid, int *socket)
{
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		err(1, "socketpair");
	}

	pid_t fork_rv = fork();
	if (fork_rv == 0) {
		close(sockets[0]);

		char *child_socket_as_str;
		asprintf(&child_socket_as_str, "%d", sockets[1]);
		if (child_socket_as_str == NULL) {
			_exit(1);
		}
		execl(pnp_child_path, "pnp_child", child_socket_as_str, NULL);
	}
	else if (fork_rv == -1) {
		err(1, "fork");
	}

	close(sockets[1]);
	*child_pid = fork_rv;
	*socket = sockets[0];
}

static void
kill_pnp_child(pid_t child_pid)
{
	kill(child_pid, SIGTERM);

	while (1) {
		pid_t wait_rv;
		wait_rv = waitpid(child_pid, NULL, 0);
		if (wait_rv != -1) {
			break;
		}
		else if (errno != EINTR) {
			err(1, "waitpid");
		}
	}
}

static void
wait_for_message(struct imsgbuf *ibuf, MESSAGE_TYPE expected_type,
    pid_t child_pid)
{
	int hello_received = 0;
	while (! hello_received) {
		if (imsg_read(ibuf) == -1) {
			kill_pnp_child(child_pid);
			exit(1);
		}
		struct imsg message;
		ssize_t imsg_get_rv = imsg_get(ibuf, &message);
		if (imsg_get_rv == -1) {
			kill_pnp_child(child_pid);
			exit(1);
		}
		if (imsg_get_rv > 0) {
			int type = (int)message.hdr.type;
			if (type == MSG_FATAL) {
				errx(1, "fatal error: %s",
				    (char *)message.data);
			}
			ck_assert_int_eq(type, expected_type);
			hello_received = 1;
			imsg_free(&message);
		}
	}
}

void
enqueue_command(struct imsgbuf *ibuf, CMD_MESSAGE_TYPE type, int fd_to_send,
    pid_t child_pid)
{
	if (imsg_compose(ibuf, (uint32_t)type, 0, getpid(), fd_to_send, NULL,
	    0) == -1) {
		kill_pnp_child(child_pid);
		err(1, "imsg_compose");
	}
}

void
send_commands(struct imsgbuf *ibuf, pid_t child_pid)
{
	if (imsg_flush(ibuf) == -1) {
		kill_pnp_child(child_pid);
		err(1, "imsg_flush");
	}
}

START_TEST (child_main_exits_if_no_filedescriptor_is_given)
{
	execl(pnp_child_path, "pnp_child", NULL);
}
END_TEST

START_TEST (child_main_exits_if_a_bogus_file_descriptor_is_given)
{
	const char *bad_fds[] = {"-1", "23", "100"};
	execl(pnp_child_path, "pnp_child", bad_fds[_i], NULL);
}
END_TEST

START_TEST (child_main_exits_if_the_fd_is_not_a_socket)
{
	int fd = open_input_file("testdata/test.flac");
	char *fdstr;
	asprintf(&fdstr, "%d", fd);
	if (fdstr == NULL) {
		err(2, "asprintf");
	}
	execl(pnp_child_path, "pnp_child", fdstr, NULL);
}
END_TEST

START_TEST (child_sends_hello_message)
{
	pid_t child_pid;
	int socket;
	start_pnp_child(&child_pid, &socket);

	struct imsgbuf ibuf;
	imsg_init(&ibuf, socket);

	wait_for_message(&ibuf, MSG_HELLO, child_pid);

	kill_pnp_child(child_pid);
}
END_TEST

int sigchld_received;

static void
signal_handler(int sigraised)
{
	if (sigraised == SIGCHLD) {
		sigchld_received = 1;
	}
}

START_TEST (child_exits_on_CMD_EXIT)
{
	pid_t child_pid;
	int socket;
	start_pnp_child(&child_pid, &socket);

	struct imsgbuf ibuf;
	imsg_init(&ibuf, socket);

	wait_for_message(&ibuf, MSG_HELLO, child_pid);

	sigchld_received = 0;
	signal(SIGCHLD, signal_handler);
	enqueue_command(&ibuf, CMD_EXIT, -1, child_pid);
	send_commands(&ibuf, child_pid);

	sleep(1);

	ck_assert_int_ne(sigchld_received, 0);
	int child_status;
	while (1) {
		pid_t wait_rv = waitpid(child_pid, &child_status, 0);
		if (wait_rv != -1) {
			break;
		}
		else if (errno != EINTR) {
			err(1, "waitpid");
		}
	}
	ck_assert_int_ne(WIFEXITED(child_status), 0);
	ck_assert_int_eq(WEXITSTATUS(child_status), 0);
}
END_TEST

START_TEST (child_main_decodes_to_file)
{
	const char *const output_files[] = {"scratchspace/test.raw",
	    "scratchspace/test.wav"};
	int in_fd = open_input_file("testdata/test.flac");
	int out_fd = open_output_file(output_files[_i]);

	pid_t child_pid;
	int socket;
	start_pnp_child(&child_pid, &socket);

	struct imsgbuf ibuf;
	imsg_init(&ibuf, socket);

	wait_for_message(&ibuf, MSG_HELLO, child_pid);

	enqueue_command(&ibuf, CMD_SET_INPUT, in_fd, child_pid);
	if (_i == 0) {
		enqueue_command(&ibuf, CMD_SET_OUTPUT_FILE_RAW, out_fd,
		    child_pid);
	}
	else {
		enqueue_command(&ibuf, CMD_SET_OUTPUT_FILE_WAV, out_fd,
		    child_pid);
	}
	enqueue_command(&ibuf, CMD_PLAY, -1, child_pid);
	send_commands(&ibuf, child_pid);

	wait_for_message(&ibuf, MSG_DONE, child_pid);

	kill_pnp_child(child_pid);

	int cmp_rv;
	const char *const compare[] = {"testdata/test.raw",
	    "testdata/test.wav"};
	char *command;
	asprintf(&command, "cmp %s %s", output_files[_i],
	    compare[_i]);
	if (command == NULL) {
		err(1, "malloc");
	}
	cmp_rv = system(command);
	ck_assert_int_eq(cmp_rv, 0);
}
END_TEST

START_TEST (child_main_decodes_to_file_repeatedly)
{
	const char *const output_files[] = {"scratchspace/test.raw",
	    "scratchspace/test.wav"};
	int in_fd = open_input_file("testdata/test.flac");
	int out_fd = open_output_file(output_files[_i]);

	pid_t child_pid;
	int socket;
	start_pnp_child(&child_pid, &socket);

	struct imsgbuf ibuf;
	imsg_init(&ibuf, socket);

	wait_for_message(&ibuf, MSG_HELLO, child_pid);

	enqueue_command(&ibuf, CMD_SET_INPUT, in_fd, child_pid);
	if (_i == 0) {
		enqueue_command(&ibuf, CMD_SET_OUTPUT_FILE_RAW, out_fd,
		    child_pid);
	}
	else {
		enqueue_command(&ibuf, CMD_SET_OUTPUT_FILE_WAV, out_fd,
		    child_pid);
	}
	enqueue_command(&ibuf, CMD_PLAY, -1, child_pid);
	send_commands(&ibuf, child_pid);

	wait_for_message(&ibuf, MSG_DONE, child_pid);

	in_fd = open_input_file("testdata/test.flac");
	out_fd = open_output_file(output_files[_i]);
	enqueue_command(&ibuf, CMD_SET_INPUT, in_fd, child_pid);
	if (_i == 0) {
		enqueue_command(&ibuf, CMD_SET_OUTPUT_FILE_RAW, out_fd,
		    child_pid);
	}
	else {
		enqueue_command(&ibuf, CMD_SET_OUTPUT_FILE_WAV, out_fd,
		    child_pid);
	}
	enqueue_command(&ibuf, CMD_PLAY, -1, child_pid);
	send_commands(&ibuf, child_pid);

	wait_for_message(&ibuf, MSG_DONE, child_pid);

	kill_pnp_child(child_pid);

	int cmp_rv;
	const char *const compare[] = {"testdata/test.raw",
	    "testdata/test.wav"};
	char *command;
	asprintf(&command, "cmp %s %s", output_files[_i],
	    compare[_i]);
	if (command == NULL) {
		err(1, "malloc");
	}
	cmp_rv = system(command);
	ck_assert_int_eq(cmp_rv, 0);
}
END_TEST

START_TEST (child_main_can_decode_to_raw_then_wav)
{
	int in_fd = open_input_file("testdata/test.flac");
	int out_fd = open_output_file("scratchspace/test.raw");

	pid_t child_pid;
	int socket;
	start_pnp_child(&child_pid, &socket);

	struct imsgbuf ibuf;
	imsg_init(&ibuf, socket);

	wait_for_message(&ibuf, MSG_HELLO, child_pid);

	enqueue_command(&ibuf, CMD_SET_INPUT, in_fd, child_pid);
	enqueue_command(&ibuf, CMD_SET_OUTPUT_FILE_RAW, out_fd, child_pid);
	enqueue_command(&ibuf, CMD_PLAY, -1, child_pid);
	send_commands(&ibuf, child_pid);

	wait_for_message(&ibuf, MSG_DONE, child_pid);

	in_fd = open_input_file("testdata/test.flac");
	out_fd = open_output_file("scratchspace/test.wav");
	enqueue_command(&ibuf, CMD_SET_INPUT, in_fd, child_pid);
	enqueue_command(&ibuf, CMD_SET_OUTPUT_FILE_WAV, out_fd, child_pid);
	enqueue_command(&ibuf, CMD_PLAY, -1, child_pid);
	send_commands(&ibuf, child_pid);

	wait_for_message(&ibuf, MSG_DONE, child_pid);

	kill_pnp_child(child_pid);

	int cmp_rv;
	cmp_rv = system("cmp testdata/test.wav scratchspace/test.wav");
	ck_assert_int_eq(cmp_rv, 0);
}
END_TEST

START_TEST (child_main_can_decode_to_wav_then_raw)
{
	int in_fd = open_input_file("testdata/test.flac");
	int out_fd = open_output_file("scratchspace/test.wav");

	pid_t child_pid;
	int socket;
	start_pnp_child(&child_pid, &socket);

	struct imsgbuf ibuf;
	imsg_init(&ibuf, socket);

	wait_for_message(&ibuf, MSG_HELLO, child_pid);

	enqueue_command(&ibuf, CMD_SET_INPUT, in_fd, child_pid);
	enqueue_command(&ibuf, CMD_SET_OUTPUT_FILE_WAV, out_fd, child_pid);
	enqueue_command(&ibuf, CMD_PLAY, -1, child_pid);
	send_commands(&ibuf, child_pid);

	wait_for_message(&ibuf, MSG_DONE, child_pid);

	in_fd = open_input_file("testdata/test.flac");
	out_fd = open_output_file("scratchspace/test.raw");
	enqueue_command(&ibuf, CMD_SET_INPUT, in_fd, child_pid);
	enqueue_command(&ibuf, CMD_SET_OUTPUT_FILE_RAW, out_fd, child_pid);
	enqueue_command(&ibuf, CMD_PLAY, -1, child_pid);
	send_commands(&ibuf, child_pid);

	wait_for_message(&ibuf, MSG_DONE, child_pid);

	kill_pnp_child(child_pid);

	int cmp_rv;
	cmp_rv = system("cmp testdata/test.raw scratchspace/test.raw");
	ck_assert_int_eq(cmp_rv, 0);
}
END_TEST

Suite
*test_suite(void)
{
	Suite *s;
	TCase *tc_init, *tc_decode;

	s = suite_create("child main");
	tc_init = tcase_create("Initialization");

	tcase_add_exit_test(tc_init,
	    child_main_exits_if_no_filedescriptor_is_given, 1);
	tcase_add_loop_exit_test(tc_init,
	    child_main_exits_if_a_bogus_file_descriptor_is_given, 1, 0, 3);
	tcase_add_exit_test(tc_init, child_main_exits_if_the_fd_is_not_a_socket,
	    1);
	tcase_add_test(tc_init, child_sends_hello_message);
	tcase_add_test(tc_init, child_exits_on_CMD_EXIT);

	tc_decode = tcase_create("Decoding");
	tcase_add_loop_test(tc_decode, child_main_decodes_to_file, 0, 2);
	tcase_add_loop_test(tc_decode, child_main_decodes_to_file_repeatedly,
	    0, 2);
	tcase_add_test(tc_decode, child_main_can_decode_to_raw_then_wav);
	tcase_add_test(tc_decode, child_main_can_decode_to_wav_then_raw);

	suite_add_tcase(s, tc_init);
	suite_add_tcase(s, tc_decode);
	
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
