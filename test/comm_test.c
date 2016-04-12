#include <sys/socket.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <assert.h>
#include <check.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/comm.h"

START_TEST (send_and_receive_data)
{
	struct pollfd	pfd;
	struct imsg	msg;

	char		testmsg[] = "This is a test.";
	pid_t		child_pid;
	int		sv[2], n_ready, child_status;

	socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv);
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		close(sv[0]);
		pfd.fd = sv[1];
		pfd.events = POLLOUT;
		comm_init(sv[1]);
		if (queue_msg(MSG_WARN, testmsg, sizeof(testmsg), -1))
			_exit(1);
		if ((n_ready = poll(&pfd, 1, 10)) == -1)
			err(1, "poll");
		if (n_ready == 0 || (pfd.revents & POLLOUT) == 0)
			_exit(1);
		if (send_msgs())
			_exit(1);
		_exit(0);
	default:
		/* Parent process */
		close(sv[1]);
		pfd.fd = sv[0];
		pfd.events = POLLIN;
		comm_init(sv[0]);
		if ((n_ready = poll(&pfd, 1, 100)) == -1)
			err(1, "poll");
		ck_assert_int_eq(n_ready, 1);
		ck_assert_int_ne(pfd.revents & (POLLIN|POLLHUP), 0);
		ck_assert_int_eq(recv_msg(&msg), 1);
		ck_assert_int_eq((int)msg.hdr.type, MSG_WARN);
		ck_assert_uint_eq(msg.hdr.len - sizeof(msg.hdr),
		    sizeof(testmsg));
		ck_assert_int_eq(memcmp(testmsg, msg.data, sizeof(testmsg)), 0);
		while (waitpid(child_pid, &child_status, 0) == -1) {
			if (errno != EINTR && errno != ECHILD)
				break;
		}
		ck_assert_int_eq(child_status, 0);
	}
}
END_TEST

Suite
*ipc_suite(void)
{
	Suite *s;
	TCase *tc_send_recv;

	s = suite_create("Interprocess Communication");
	tc_send_recv = tcase_create("Send and Receive");

	tcase_add_test(tc_send_recv, send_and_receive_data);
	suite_add_tcase(s, tc_send_recv);
	
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
