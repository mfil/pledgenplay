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
#include "child_errors.h"
#include "child_messages.h"
#include "comm.h"
#include "file.h"
#include "flac.h"
#include "pnp.h"

static void	fill_inbuf(struct input *);
static void	clear_inbuf(struct input *);
static void	new_file(int, struct input *);
static int	extract_meta(struct input *);

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
		child_fatal("malloc");
	in->fd = -1;
	in->fmt = UNKNOWN;
	in->buf = malloc(INBUF_SIZE);
	if (in->buf == NULL)
		child_fatal("malloc");
	in->buf_size = in->buf_free = INBUF_SIZE;
	in->read_pos = in->write_pos = 0;
	in->eof = in->error = 0;

	initialize_ipc(sv[1]);
	nfds = 2 + (out->type == OUT_SNDIO ? sio_nfds(out->handle.sio) : 0);
	if ((pfd = calloc(nfds, sizeof(struct pollfd))) == NULL)
		ipc_error("calloc");
	pfd[0].fd = sv[1];
	pfd[0].events = POLLIN|POLLOUT;
	pfd[1].fd = -1;
	pfd[1].events = POLLIN;
	if (out->type == OUT_SNDIO) {
		if (sio_pollfd(out->handle.sio, pfd+2, POLLOUT) == 0)
			ipc_error("sio_pollfd");
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
				child_warnx("Not implemented.");
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
			child_fatalx("sio_pollfd: failed");
	}
	nready = poll(pfd, nfds, 0);
	if (nready == -1) {
		ipc_error("poll");
	}
	if (pfd[0].revents & (POLLIN|POLLHUP))
		receive_messages();
	if (pfd[0].revents & POLLOUT)
		send_messages();
	if (in->fd == pfd[1].fd && (pfd[1].revents & (POLLIN|POLLHUP))) {
		fill_inbuf(in);
	}

	struct message message;
	while(get_next_message(&message)) {
		switch (message.type) {
		case (NEW_FILE):
			if (state->callback) {
				state->task_new_file = 1;
				state->new_fd = message.data.fd;
			}
			else {
				new_file(message.data.fd, in);
				state->play = STOPPED;
			}
			break;
		case (CMD_META):
			if (in->fd == -1)
				enqueue_message(MSG_NACK, "No input file");
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
		default:
			child_fatalx("Unexpected or invalid message type.");
		}
	}
	if (state->play == PLAYING && out->type == OUT_SNDIO) {
		sio_ev = sio_revents(out->handle.sio, pfd+2);
		if (sio_ev & POLLHUP)
			child_fatalx("sndio device gone");
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
			child_warn("close");
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
		enqueue_message(MSG_NACK, "");
		if (in->fd != -1 && close(in->fd) != 0)
			child_warn("close");
		in->fd = -1;
		in->fmt = UNKNOWN;
	}
	else
		enqueue_message(MSG_ACK, "");
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
	enqueue_message(MSG_DONE, "");
	return (rv);
}
