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
#include <fcntl.h>
#include <imsg.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../message_types.h"

const char *const pnp_child_path = "../obj/pnp_child";

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
wait_for_message(MESSAGE_TYPE expected_type, pid_t child_pid, int socket,
    struct imsgbuf *ibuf)
{
	struct pollfd pollfd = {socket, POLLIN, 0};
	int hello_received = 0;
	while (! hello_received) {
		if (poll(&pollfd, 1, 0) == -1) {
			kill(child_pid, SIGTERM);
			exit(1);
		}
		if (pollfd.revents & POLLIN) {
			if (imsg_read(ibuf) == -1) {
				kill(child_pid, SIGTERM);
				exit(1);
			}
			struct imsg message;
			ssize_t imsg_get_rv = imsg_get(ibuf, &message);
			if (imsg_get_rv == -1) {
				kill(child_pid, SIGTERM);
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
	int fd = open("testdata/test.flac", O_RDONLY);
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

	wait_for_message(MSG_HELLO, child_pid, socket, &ibuf);

	kill(child_pid, SIGTERM);
}
END_TEST

int sigchld_received;

void
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

	wait_for_message(MSG_HELLO, child_pid, socket, &ibuf);

	sigchld_received = 0;
	signal(SIGCHLD, signal_handler);
	imsg_compose(&ibuf, (int)CMD_EXIT, 0, getpid(), -1, NULL, 0);
	if (imsg_flush(&ibuf) != 0) {
		kill(child_pid, SIGTERM);
		err(1, "imsg_flush");
	}
	sleep(1);
	ck_assert_int_ne(sigchld_received, 0);
	int child_status;
	pid_t wait_rv = waitpid(child_pid, &child_status, 0);
	ck_assert_int_eq(wait_rv, child_pid);
	ck_assert_int_ne(WIFEXITED(child_status), 0);
	ck_assert_int_eq(WEXITSTATUS(child_status), 0);
}
END_TEST

START_TEST (child_main_decodes_to_raw_audio_file)
{
	int in_fd = open("testdata/test.flac", O_RDONLY);
	if (in_fd == -1) {
		err(1, "open");
	}
	int out_fd = open("scratchspace/test.raw",
	    O_WRONLY | O_CREAT | O_TRUNC,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (out_fd == -1) {
		err(1, "open");
	}

	pid_t child_pid;
	int socket;
	start_pnp_child(&child_pid, &socket);

	struct imsgbuf ibuf;
	imsg_init(&ibuf, socket);

	wait_for_message(MSG_HELLO, child_pid, socket, &ibuf);

	int rv;
	rv = imsg_compose(&ibuf, (uint32_t)CMD_SET_INPUT, 0, getpid(), in_fd,
	    NULL, 0);
	if (rv != 1) {
		kill(child_pid, SIGTERM);
		err(1, "imsg_compose");
	}
	rv = imsg_flush(&ibuf);
	if (rv != 0) {
		kill(child_pid, SIGTERM);
		err(1, "imsg_flush");
	}

	rv = imsg_compose(&ibuf, (uint32_t)CMD_SET_OUTPUT_FILE_RAW, 0, getpid(),
	    out_fd, NULL, 0);
	if (rv == -1) {
		kill(child_pid, SIGTERM);
		err(1, "imsg_compose");
	}
	rv = imsg_flush(&ibuf);
	if (rv == -1) {
		kill(child_pid, SIGTERM);
		err(1, "imsg_flush");
	}

	rv = imsg_compose(&ibuf, (uint32_t)CMD_PLAY, 0, getpid(), -1, NULL, 0);
	if (rv == -1) {
		kill(child_pid, SIGTERM);
		err(1, "imsg_compose");
	}
	rv = imsg_flush(&ibuf);
	if (rv == -1) {
		kill(child_pid, SIGTERM);
		err(1, "imsg_flush");
	}

	wait_for_message(MSG_DONE, child_pid, socket, &ibuf);

	kill(child_pid, SIGTERM);

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
	tcase_add_test(tc_decode, child_main_decodes_to_raw_audio_file);

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
