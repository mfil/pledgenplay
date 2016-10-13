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

#define INBUF_SIZE	32768 /* 32 kB */

#include <sys/types.h>
#include <sys/uio.h> /* for readv(2) */
#include <sys/queue.h>
#include <sys/uio.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <imsg.h>
#include <poll.h>
#include <sndio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "child.h"
#include "comm.h"
#include "file.h"
#include "flac.h"
#include "pnp.h"

static void	process_parent_msg(struct state *, struct input *);
static void	fill_inbuf(struct input *);
static void	clear_inbuf(struct input *);
static void	new_file(int, struct input *);
static int	extract_meta(struct input *);

static struct imsgbuf	ibuf;
static struct pollfd	*pfd;
static nfds_t		nfds;
struct input		*in;

int
child_main(int sv[2], struct out *out)
{
	struct state	state;

	if (out->type == OUT_SNDIO && pledge("stdio recvfd audio", NULL) == -1)
		return (-1);
	else if (pledge("stdio recvfd", NULL) == -1)
		return (-1);
	close(0);
	close(1);
	close(sv[0]);
	memset(&state, 0, sizeof(state));

	in = malloc(sizeof(struct input));
	if (in == NULL)
		fatal("malloc");
	in->fd = -1;
	in->fmt = UNKNOWN;
	in->buf = malloc(INBUF_SIZE);
	if (in->buf == NULL)
		fatal("malloc");
	in->buf_size = in->buf_free = INBUF_SIZE;
	in->read_pos = in->write_pos = 0;
	in->eof = in->error = 0;

	imsg_init(&ibuf, sv[1]);
	nfds = 2 + (out->type == OUT_SNDIO ? sio_nfds(out->handle.sio) : 0);
	if ((pfd = calloc(nfds, sizeof(struct pollfd))) == NULL)
		_err("calloc");
	pfd[0].fd = sv[1];
	pfd[0].events = POLLIN|POLLOUT;
	pfd[1].fd = -1;
	pfd[1].events = POLLIN;
	if (out->type == OUT_SNDIO) {
		if (sio_pollfd(out->handle.sio, pfd+2, POLLOUT) == 0)
			_err("sio_pollfd");
	}

	while (1) {
		process_events(in, out, &state);
		if (state.task_start_play) {
			state.task_start_play = 0;
			switch (in->fmt) {
			case (FLAC):
				play_flac(in, out, &state);
				break;
			default:
				msgwarnx("Not implemented.");
			}
		}
	}
}

void
process_events(struct input *in, struct out *out, struct state *state)
{
	int		nready;
	int		sio_ev;

	out->ready = 0;
	if (!state->callback && state->task_new_file) {
		new_file(state->new_fd, in);
		state->task_new_file = 0;
		state->play = STOPPED;
	}

	if (out->type == OUT_SNDIO) {
		if (sio_pollfd(out->handle.sio, pfd+2, POLLOUT) == 0)
			fatalx("sio_pollfd: failed");
	}
	nready = poll(pfd, nfds, 0);
	if (nready == -1) {
		_err("poll");
	}
	if (pfd[0].revents & (POLLIN|POLLHUP))
		process_parent_msg(state, in);
	if (pfd[0].revents & POLLOUT)
		if (imsg_flush(&ibuf) == -1)
			_err("imsg_flush");
	if (in->fd == pfd[1].fd && (pfd[1].revents & (POLLIN|POLLHUP))) {
		fill_inbuf(in);
	}
	if (state->play == PLAYING && out->type == OUT_SNDIO) {
		sio_ev = sio_revents(out->handle.sio, pfd+2);
		if (sio_ev & POLLHUP)
			fatalx("sndio device gone");
		if (sio_ev & POLLOUT)
			out->ready = 1;
	}
	/*
	 * Update pfd[1].fd in case a new input file was supplied
	 * or file_err() was called.
	 */
	pfd[1].fd = in->fd;
}

static void
process_parent_msg(struct state *state, struct input *in)
{
	struct imsg	imsg;
	ssize_t		nbytes;

	if (imsg_read(&ibuf) == -1 && errno != EAGAIN)
		_err("imsg_read");
	while ((nbytes = imsg_get(&ibuf, &imsg)) > 0) {
		switch ((int)imsg.hdr.type) {
		case (NEW_FILE):
			if (state->callback) {
				state->task_new_file = 1;
				state->new_fd = imsg.fd;
			}
			else {
				new_file(imsg.fd, in);
				state->play = STOPPED;
			}
			break;
		case (CMD_META):
			if (in->fd == -1)
				msgstr(MSG_NACK, "No input file");
			else
				extract_meta(in);
			break;
		case (CMD_PLAY):
			if (in->fd == -1)
				file_errx(in, "No input file");
			else if (state->play == PAUSED)
				state->play = RESUME;
			else if (state->play == PAUSING)
				state->play = PLAYING;
			else
				state->task_start_play = 1;
			break;
		case (CMD_PAUSE):
			state->play = PAUSING;
			break;
		case (CMD_EXIT):
			_exit(0);
		}
		imsg_free(&imsg);
	}
}

static void
fill_inbuf(struct input *in)
{
	struct iovec	iov[2];
	size_t		nbytes;
	size_t		r_pos = in->read_pos;
	size_t		w_pos = in->write_pos;
	size_t		size = in->buf_size;

	if (in->buf_free == 0 || in->eof || in->fd == -1)
		return;
	iov[0].iov_base = in->buf + w_pos;
	iov[1].iov_base = in->buf;
	if (w_pos < r_pos) {
		iov[0].iov_len = r_pos - w_pos;
		iov[1].iov_len = 0;
	}
	else {
		iov[0].iov_len = size - w_pos;
		iov[1].iov_len = r_pos;
	}
	nbytes = readv(in->fd, iov, 2);
	if (nbytes < 0) {
		if (errno != EAGAIN) {
			file_err(in, "readv");
			in->error = 1;
		}
		return;
	}
	if (nbytes == 0) {
		in->eof = 1;
		return;
	}
	in->buf_free -= nbytes;
	in->write_pos = (w_pos + nbytes) % size;
}

static void
clear_inbuf(struct input *in)
{
	in->read_pos = in->write_pos = 0;
	in->eof = in->error = 0;
	in->buf_free = in->buf_size;
}

static void
new_file(int fd, struct input *in)
{
	/* Close the old file (if there was one). */
	if (in->fd != -1) {
		if (close(in->fd) != 0)
			msgwarn("close");
		in->fd = -1;
		in->fmt = UNKNOWN;
		clear_inbuf(in);
	}
	/* Set the new one. */
	in->fd = fd;
	/* Determine the file format. */
	if ((in->fmt = filetype(in->fd)) == -1)
		file_err(in, "read");
	else if (in->fmt == UNKNOWN) {
		msg(MSG_NACK, NULL, 0);
		if (in->fd != -1 && close(in->fd) != 0)
			msgwarn("close");
		in->fd = -1;
		in->fmt = UNKNOWN;
	}
	else
		msg(MSG_ACK, NULL, 0);
}

static int
extract_meta(struct input *in)
{
	int	rv;

	switch (in->fmt) {
	case (FLAC):
		rv = extract_meta_flac(in);
		break;
	default:
		rv = -1;
	}
	if (rv == -1)
		return (-1);
	msg(MSG_DONE, NULL, 0);
	return (rv);
}

/*
 * Compose a message to the parent. The message will be truncated if it exceeds
 * the maximum length for imsg_compose (64 kb).
 */
void
msg(int type, void *data, size_t len)
{
	if (len > UINT16_MAX)
		len = UINT16_MAX;
	if (imsg_compose(&ibuf, (u_int32_t)type, 0, getpid(), -1, data,
	    (u_int16_t)len) == -1)
		_err("imsg");
}

/* Send msg to the parent; msg must be null-terminated. */
void
msgstr(int type, char *msg)
{
	if (imsg_compose(&ibuf, (u_int32_t)type, 0, getpid(), -1, msg,
	    (u_int16_t)strlen(msg)+1) == -1)
		_err("imsg");
}

void
msgwarn(char *msg)
{
	struct ibuf	*buf;
	char		*errmsg;
	u_int16_t	msg_len, err_len;

	errmsg = strerror(errno);
	msg_len = (u_int16_t)strlen(msg);
	err_len = (u_int16_t)strlen(errmsg);
	buf = imsg_create(&ibuf, (u_int32_t)MSG_WARN, 0, getpid(),
	    msg_len + err_len + 3);
	if (buf == NULL)
		_err("imsg");
	if (imsg_add(buf, msg, msg_len) == -1 ||
	    imsg_add(buf, ": ", 2) == -1 ||
	    imsg_add(buf, errmsg, strlen(errmsg)+1) == -1)
		_err("imsg");
	imsg_close(&ibuf, buf);
}

void
msgwarnx(char *msg)
{
	if (imsg_compose(&ibuf, (u_int32_t)MSG_WARN, 0, getpid(), -1, msg,
	    strlen(msg)+1) == -1)
	    	_err("imsg");
}

void
file_err(struct input *in, char *msg)
{
	struct ibuf	*buf;
	char		*errmsg;
	u_int16_t	msg_len, err_len;

	errmsg = strerror(errno);
	msg_len = (u_int16_t)strlen(msg);
	err_len = (u_int16_t)strlen(errmsg);
	buf = imsg_create(&ibuf, (u_int32_t)MSG_FILE_ERR, 0, getpid(),
	    msg_len + err_len + 3);
	if (buf == NULL)
		_err("imsg");
	if (imsg_add(buf, msg, msg_len) == -1 ||
	    imsg_add(buf, ": ", 2) == -1 ||
	    imsg_add(buf, errmsg, strlen(errmsg)+1) == -1)
		_err("imsg");
	imsg_close(&ibuf, buf);
	if (in->fd != -1 && close(in->fd) != 0)
		msgwarn("close");
	in->fd = -1;
	in->fmt = UNKNOWN;
}

void
file_errx(struct input *in, char *msg)
{
	if (imsg_compose(&ibuf, (u_int32_t)MSG_FILE_ERR, 0, getpid(), -1, msg,
	    (u_int16_t)strlen(msg)+1) == -1)
		_err("imsg");
	if (in->fd != -1 && close(in->fd) != 0)
		msgwarn("close");
	in->fd = -1;
	in->fmt = UNKNOWN;
}

__dead void
fatal(char *msg)
{
	struct ibuf	*buf;
	char		*errmsg;
	u_int16_t	msg_len, err_len;

	errmsg = strerror(errno);
	msg_len = (u_int16_t)strlen(msg);
	err_len = (u_int16_t)strlen(errmsg);
	buf = imsg_create(&ibuf, (u_int32_t)MSG_FATAL, 0, getpid(),
	    msg_len + err_len + 3);
	if (buf == NULL)
		_err("imsg");
	if (imsg_add(buf, msg, msg_len) == -1 ||
	    imsg_add(buf, ": ", 2) == -1 ||
	    imsg_add(buf, errmsg, strlen(errmsg)+1) == -1)
		_err("imsg");
	imsg_close(&ibuf, buf);
	if (imsg_flush(&ibuf) == -1)
		_err("imsg");
	_exit(1);
}

__dead void
fatalx(char *msg)
{
	char		*errmsg;
	u_int16_t	msg_len, err_len;

	errmsg = strerror(errno);
	msg_len = (u_int16_t)strlen(msg);
	err_len = (u_int16_t)strlen(errmsg);
	if (imsg_compose(&ibuf, (u_int32_t)MSG_FILE_ERR, 0, getpid(), -1, msg,
	    (u_int16_t)strlen(msg)+1) == -1)
		_err("imsg");
	if (imsg_flush(&ibuf) == -1)
		_err("imsg");
	_exit(1);
}

/*
 * If the child process cannot communicate with the parent anymore, we try to
 * write an error message to stderr and exit.
 */
__dead void
_err(char *msg)
{
	dprintf(2, "pnp child: %s: %s\n", msg, strerror(errno));
	_exit(1);
}
