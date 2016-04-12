#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <imsg.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define LEN	1024

enum msg {SAYHELLO, TEXT};

void child(int[2]);
void parent(pid_t, int[2]);

int
main(int argc, char *argv[])
{
	int		sv[2];
	pid_t		child_pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sv) == -1)
		err(1, "socketpair");

	if ((child_pid = fork()) == -1)
		err(1, "fork");
	if (child_pid == 0) {
		/* Child process */
		child(sv);
		/* NOTREACHED */
	}
	/* Parent process */
	parent(child_pid, sv);
	exit(0);
}

void
child(int sv[2])
{
	struct imsgbuf	ibuf;
	struct imsg	msg;
	struct pollfd	read_fd;

	int		nready;
	ssize_t		len;

	close(sv[0]);
	read_fd.fd = sv[1];
	read_fd.events = POLLIN;
	read_fd.revents = 0;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");
	imsg_init(&ibuf, sv[1]);
	for (;;) {
		if ((nready = poll(&read_fd, 1, 0)) == -1)
			err(1, "poll");
		if (nready == 0) {
			continue;
		}
		if (read_fd.revents & (POLLERR|POLLNVAL|POLLHUP)) {
			err(1, "bad event");
		}
		if (read_fd.revents & POLLIN) {
			if ((len = imsg_read(&ibuf)) <= 0 && errno != EAGAIN)
				err(1, "imsg_read");
			if ((len = imsg_get(&ibuf, &msg)) == -1)
				err(1, "imsg_get");
			if (msg.hdr.type == SAYHELLO)
				printf("Hello, %u!\n", msg.hdr.pid);
			break;
		}
	}
	_exit(0);
}

void
parent(pid_t child_pid, int sv[2])
{
	struct imsgbuf	ibuf;
	struct imsg	*msg;

	close(sv[1]);
	imsg_init(&ibuf, sv[0]);
	if (imsg_compose(&ibuf, (uint32_t)SAYHELLO, 0, getpid(), -1, NULL, 0) == -1)
		err(1, "imsg_compose");
	if (msgbuf_write(&ibuf.w) <= 0)
		err(1, "msgbuf_write");
	printf("message written\n");
	while (wait(NULL) == -1) {
		if (errno == ECHILD)
			break;
		if (errno != EINTR)
			err(1, "wait");
	}
	return;
}
