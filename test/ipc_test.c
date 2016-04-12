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
	struct pollfd	pfd;
	struct imsgbuf	ibuf;

	pid_t		child_pid;
	int		sv[2], rv, n_ready, status;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		rv = child_main(sv, 0, -1);
		if (rv == -1)
			ck_abort_msg("child_main failed.");
	default:
		/* Parent process */
		close(sv[1]);
		pfd.fd = sv[0];
		pfd.events = POLLOUT;
		imsg_init(&ibuf, sv[0]);
		imsg_compose(&ibuf, (u_int32_t)CMD_EXIT, 0, getpid(), -1, NULL,
		    0);
		if ((n_ready = poll(&pfd, 1, 1)) == -1)
			err(1, "poll");
		if (n_ready == 0 || (pfd.revents & POLLOUT) == 0)
			errx(1, "Can't write to socket.");
		if (imsg_flush(&ibuf) == -1) {
			errx(1, "imsg_flush failed.");
		}
		sleep(1);
		while ((rv = waitpid(child_pid, &status, WNOHANG)) == -1) {
			if (errno != EINTR)
				err(1, "waitpid");
		}
		if (rv == 0) {
			ck_abort_msg("Child process did not exit.");
		}
		ck_assert_int_eq(status, 0);
	}
}
END_TEST

START_TEST (parent_handles_child_exit)
{
	pid_t	child_pid;
	int	sv[2], rv, errval, child_status;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		_exit(1);
	default:
		/* Parent process */
		rv = parent_main(sv, child_pid, &errval, &child_status);
		ck_assert_int_eq(rv, SIGNAL);
		ck_assert_int_eq(errval, SIGCHLD);
		ck_assert(WIFEXITED(child_status));
		ck_assert_int_eq(WEXITSTATUS(child_status), 1);
	}
}
END_TEST

int 	check_child(pid_t, int *, int *);
START_TEST (parent_ignores_false_SIGCHLD)
{
	pid_t	child_pid;
	int	sv[2], errval, child_status;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		kill(getppid(), SIGCHLD);
	default:
		/* Parent process */
		ck_assert_int_eq(check_child(child_pid, &errval, &child_status), 0);
	}
}
END_TEST

/*
 * When the parent process receives a signal that causes it to terminate,
 * parent_main() should send SIGTERM to the child process, wait for it, and
 * then return SIGNAL.
 */
int	term_signals[] = {SIGHUP, SIGINT, SIGTERM};

START_TEST (parent_handles_term_signal)
{
	pid_t	child_pid, wait_rv;
	int	sv[2], rv, errval, child_status;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		kill(getppid(), term_signals[_i]);
		/* Loop until terminated. */
		while (1)
			;
	default:
		/* Parent process */
		rv = parent_main(sv, child_pid, &errval, &child_status);
		ck_assert_int_eq(rv, SIGNAL);
		ck_assert_int_eq(errval, term_signals[_i]);
		ck_assert(WIFSIGNALED(child_status));
		ck_assert_int_eq(WTERMSIG(child_status), SIGTERM);
		wait_rv = waitpid(child_pid, &child_status, WNOHANG);
		ck_assert(wait_rv == -1);
		ck_assert_int_eq(errno, ECHILD);
	}
}
END_TEST

START_TEST (child_acks_new_file_if_valid)
{
	struct pollfd	pfd;
	struct imsgbuf	ibuf;
	struct imsg	msg;

	pid_t		child_pid;
	int		sv[2], rv, n_ready, fd;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		rv = child_main(sv, 0, -1);
		if (rv == -1)
			ck_abort_msg("child_main failed.");
	default:
		/* Parent process */
		close(sv[1]);
		fd = open("testdata/test.flac", O_RDONLY);
		pfd.fd = sv[0];
		pfd.events = POLLOUT;
		imsg_init(&ibuf, sv[0]);
		imsg_compose(&ibuf, (u_int32_t)NEW_FILE, 0, getpid(), fd, NULL,
		    0);
		if ((n_ready = poll(&pfd, 1, 1)) == -1)
			err(1, "poll");
		if (n_ready == 0 || (pfd.revents & POLLOUT) == 0)
			errx(1, "Can't write to socket.");
		if (imsg_flush(&ibuf) == -1) {
			errx(1, "imsg_flush failed.");
		}
		pfd.events = POLLIN;
		if ((n_ready = poll(&pfd, 1, 1)) == -1)
			err(1, "poll");
		if (n_ready == 0 || (pfd.revents & (POLLIN|POLLHUP)) == 0)
			errx(1, "Can't read from socket.");
		if (imsg_read(&ibuf) == -1)
			errx(1, "imsg_read failed.");
		if (imsg_get(&ibuf, &msg) == -1)
			errx(1, "imsg_get failed.");
		ck_assert_int_eq((int)msg.hdr.type, MSG_ACK_FILE);
	}
}
END_TEST

START_TEST (child_sends_MSG_FILE_INV_on_invalid_file)
{
	struct pollfd	pfd;
	struct imsgbuf	ibuf;
	struct imsg	msg;

	pid_t		child_pid;
	int		sv[2], rv, n_ready, fd;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		rv = child_main(sv, 0, -1);
		if (rv == -1)
			ck_abort_msg("child_main failed.");
	default:
		/* Parent process */
		close(sv[1]);
		fd = open("testdata/random_garbage", O_RDONLY);
		pfd.fd = sv[0];
		pfd.events = POLLOUT;
		imsg_init(&ibuf, sv[0]);
		imsg_compose(&ibuf, (u_int32_t)NEW_FILE, fd, getpid(), -1, NULL,
		    0);
		if ((n_ready = poll(&pfd, 1, 1)) == -1)
			err(1, "poll");
		if (n_ready == 0 || (pfd.revents & POLLOUT) == 0)
			errx(1, "Can't write to socket.");
		if (imsg_flush(&ibuf) == -1)
			errx(1, "imsg_flush failed.");
		pfd.events = POLLIN;
		if ((n_ready = poll(&pfd, 1, 1)) == -1)
			err(1, "poll");
		if (n_ready == 0 || (pfd.revents & (POLLIN|POLLHUP)) == 0)
			errx(1, "Can't read from socket.");
		if (imsg_read(&ibuf) == -1)
			errx(1, "imsg_read failed.");
		if (imsg_get(&ibuf, &msg) == -1)
			errx(1, "imsg_get failed.");
		ck_assert_int_eq((int)msg.hdr.type, MSG_FILE_INV);
	}
}
END_TEST

Suite
*ipc_suite(void)
{
	Suite *s;
	TCase *tc_cmd;
	TCase *tc_signals;

	s = suite_create("Interprocess Communication");
	tc_cmd = tcase_create("Commands");
	tc_signals = tcase_create("Signal handling");

	tcase_add_test(tc_cmd, child_exits_on_CMD_EXIT);
	tcase_add_test(tc_cmd, child_acks_new_file_if_valid);
	tcase_add_test(tc_cmd, child_sends_MSG_FILE_INV_on_invalid_file);
	tcase_add_test(tc_signals, parent_handles_child_exit);
	tcase_add_test(tc_signals, parent_ignores_false_SIGCHLD);
	tcase_add_loop_test(tc_signals, parent_handles_term_signal, 0, 3);
	suite_add_tcase(s, tc_cmd);
	suite_add_tcase(s, tc_signals);
	
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
