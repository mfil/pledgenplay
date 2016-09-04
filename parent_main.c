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

#include <sys/limits.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "comm.h"
#include "pnp.h"

static void	 	signal_handler(int);
static void		check_signal(void);

static struct imsgbuf	ibuf;
static struct pollfd	pfd;
static int		term_sig, caught_sigchld;
static pid_t		child_pid;

extern char		*__progname;

void
parent_init(int sv[2], pid_t child)
{
	/* Set up the signal handler. */
	if (signal(SIGCHLD, signal_handler) == SIG_ERR
	    || signal(SIGHUP, signal_handler) == SIG_ERR
	    || signal(SIGINT, signal_handler) == SIG_ERR
	    || signal(SIGTERM, signal_handler) == SIG_ERR)
		parent_err("signal");

	if (close(sv[1]))
		parent_err("close");
	imsg_init(&ibuf, sv[0]);
	child_pid = child;
	pfd.fd = sv[0];
	pfd.events = POLLIN|POLLOUT;
}

void
parent_msg(int type, char *data, size_t datalen)
{
	if (imsg_compose(&ibuf, (u_int32_t)type, 0, getpid(), -1, data, datalen)
	    == -1)
		parent_err("imsg_compose");
}

/*
 * parent_process_events: Handles signals and check for messages from
 * the child process. It stores the next message in the supplied struct
 * and returns the length of the message; 0 indicates that there is no
 * message. If the return value was > 0, the message data needs to be freed
 * with imsg_free() when no longer needed.
 *
 * This function should be called regularly.
 */
ssize_t
parent_process_events(struct imsg *msg)
{
	int		nready;
	ssize_t		rv = 0;

	check_signal();
	nready = poll(&pfd, 1, 0);
	if (nready == -1)
		parent_err("poll");
	if ((rv = imsg_get(&ibuf, msg)) == -1)
		parent_err("imsg_get");
	if (pfd.revents & (POLLIN|POLLHUP)) {
		if (imsg_read(&ibuf) == -1 && errno != EAGAIN)
			parent_err("imsg_read");
	}
	if (pfd.revents & POLLOUT) {
		/* Send own messages. */
		if (imsg_flush(&ibuf) == -1)
			parent_err("imsg_flush");
	}
	return (rv);
}

/*
 * check_signal: check if a signal was caught and take the appropriate
 * action.
 */
static void
check_signal(void) {
	int child_status;

	if (caught_sigchld) {
		if (check_child())
			warnx("spurious SIGCHILD caught");
		else
			exit(1);
	}
	if (term_sig) {
		if (kill(child_pid, SIGTERM))
			parent_err("kill");
		while (waitpid(child_pid, &child_status, 0) == -1) {
			if (errno != EINTR) {
				parent_err("waitpid");
			}
		}
		psignal(term_sig, __progname);
		exit(1);
	}
}

void
stop_child(void)
{
	parent_msg(CMD_EXIT, NULL, 0);
	if (imsg_flush(&ibuf) == -1)
		parent_err("imsg_flush");
}

struct meta
*get_meta()
{
	struct meta	*mdata = malloc(sizeof(struct meta));
	struct imsg	msg;
	char		**field = NULL, *trackstr;
	const char	*errstr = NULL;
	size_t		len;

	if (mdata == NULL)
		return (NULL);
	mdata->artist = NULL;
	mdata->title = NULL;
	mdata->album = NULL;
	mdata->trackno = -1;
	mdata->date = NULL;
	mdata->time = NULL;

	parent_msg(CMD_META, NULL, 0);
	while (1) {
		if (parent_process_events(&msg) > 0) {
			field = NULL;
			switch ((int)msg.hdr.type) {
			case (META_ARTIST):
				field = &mdata->artist;
				break;
			case (META_TITLE):
				field = &mdata->title;
				break;
			case (META_ALBUM):
				field = &mdata->album;
				break;
			case (META_TRACKNO):
				/* Store the track number as an int. */
				len = msg.hdr.len - IMSG_HEADER_SIZE;
				trackstr = strndup(msg.data, len);
				if (trackstr == NULL)
					parent_err("strndup");
				mdata->trackno = (int)strtonum(trackstr, 0,
				    INT_MAX, &errstr);
				if (errstr != NULL)
					mdata->trackno = -1;
				free(trackstr);
				break;
			case (META_DATE):
				field = &mdata->date;
				break;
			case (META_TIME):
				field = &mdata->time;
				break;
			case (MSG_WARN):
				len = msg.hdr.len - IMSG_HEADER_SIZE;
				child_warn(msg.data, len);
				break;
			case (META_END):
				goto done;
			case (MSG_FILE_ERR):
				goto fail;
			}
			if (field != NULL && msg.data != NULL) {
				len = msg.hdr.len - IMSG_HEADER_SIZE;
				*field = strndup(msg.data, len);
				if (*field == NULL)
					parent_err("strndup");
			}
			imsg_free(&msg);
		}
	}

done:
	imsg_free(&msg);
	return (mdata);

fail:
	imsg_free(&msg);
	free_meta(mdata);
	return (NULL);
}

int
send_new_file(char *infile)
{
	struct imsg	msg;
	int		in_fd, rv = -1;

	in_fd = open(infile, O_RDONLY|O_NONBLOCK);
	if (imsg_compose(&ibuf, (u_int32_t)NEW_FILE, 0, 0, in_fd, NULL, 0)
	    == -1)
		parent_err("imsg_compose");
	while (1) {
		if (parent_process_events(&msg) > 0) {
			switch (msg.hdr.type) {
			case (MSG_ACK_FILE):
				rv = 0;
				break;
			case (MSG_FILE_ERR):
				rv = 1;
				break;
			default:
				break;
			}
			imsg_free(&msg);
			if (rv != -1)
				return (rv);
		}
	}
	return (rv);
}

int
decode(char *infile)
{
	struct imsg	msg;

	if (send_new_file(infile))
		return (1);
	parent_msg((u_int32_t)CMD_PLAY, NULL, 0);
	while (1) {
		if (parent_process_events(&msg) > 0) {
			switch (msg.hdr.type) {
			case MSG_DONE:
				imsg_free(&msg);
				return (0);
			}
			imsg_free(&msg);
		}
	}
}

void
free_meta(struct meta *mdata)
{
	if (mdata != NULL) {
		free(mdata->artist);
		free(mdata->title);
		free(mdata->album);
		free(mdata->date);
		free(mdata->time);
		free(mdata);
	}
}

void
child_warn(char *msg, size_t len)
{
	msg[len] = '\0';
	printf("pnp child: warning: %s\n", msg);
}

void
signal_handler(int s)
{
	switch (s) {
	case (SIGCHLD):
		caught_sigchld = 1;
		break;
	case (SIGHUP):
	case (SIGINT):
	case (SIGTERM):
		term_sig = s;
		break;
	}
}

/* check_child: Return 0 if child has exited, 1 if not. */
int
check_child(void)
{
	int	status;
	pid_t	rv;

	while ((rv = waitpid(child_pid, &status, WNOHANG)) == -1) {
		if (errno != EINTR)
			parent_err("waitpid");
	}
	return (rv == child_pid ? 0 : 1);
}

/* parent_err: kill child process and exit with error message. */
__dead void
parent_err(const char *errmsg)
{
	int	child_status, saved_errno, wait_rv;

	saved_errno = errno;
	if (kill(child_pid, SIGTERM)) {
		warn("kill");
		errno = saved_errno;
		err(1, "%s", errmsg);
	}
	while ((wait_rv = waitpid(child_pid, &child_status, 0)) == -1) {
		if (errno != EINTR) {
			warn("waitpid");
			errno = saved_errno;
			err(1, "%s", errmsg);
		}
	}
	errno = saved_errno;
	err(1, "%s", errmsg);
}
