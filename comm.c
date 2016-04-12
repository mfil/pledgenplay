#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <imsg.h>
#include <unistd.h>

#include "comm.h"

/* Functions for IPC. */

static struct imsgbuf	ibuf;
static int		pid;

void
comm_init(int sock)
{
	imsg_init(&ibuf, sock);
	pid = getpid();
	return;
}

/*
 * queue_msg: Queue up a message to the other process. Unless msg_type is
 * SEND_FILE, fd is ignored. If msg_type equals SEND_FILE, fd must be >= 0.
 */
int
queue_msg(int msg_type, void *data, u_int16_t len, int fd)
{
	int	rval;
	rval = imsg_compose(&ibuf, (u_int32_t)msg_type, 0, pid,
	    msg_type == MSG_SEND_FD ? fd : -1, data, len);
	return (rval == -1 ? -1 : 0);
}

int
send_msgs(void)
{
	int	rval;
	rval = imsg_flush(&ibuf);
	return (rval == -1 ? -1 : 0);
}

int
recv_msg(struct imsg *msg)
{
	ssize_t n_bytes;
	if ((n_bytes = imsg_read(&ibuf)) == -1)
		goto fail;
	if ((n_bytes = imsg_get(&ibuf, msg)) == -1)
		goto fail;
	return (n_bytes == 0 ? 0 : 1);

fail:
	imsg_clear(&ibuf);
	_exit(1);
}
