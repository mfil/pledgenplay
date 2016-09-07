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

#ifndef PNP_CHILD_H
#define PNP_CHILD_H

#include <stdio.h>

union handle {
	FILE		*fp;
	struct sio_hdl	*sio;
};
struct out {
	int		type;
	union handle	handle;
};
struct input {
	int	fd;
	int	fmt;
	char	*buf;
	size_t	buf_size, buf_free, read_pos, write_pos;
	int	eof, error;
};

/* State for the event handler and player functions. */
enum {STOPPED, PLAYING, PAUSED};
struct state {
	int	play;     /* = STOPPED, PLAYING, or PAUSED */
	int	callback; /* Set to 1 if we are in a callback. */

	 /* Actions queued up for later. */
	int	task_new_file, new_fd;
	int	task_start_play;
};

void process_events(struct input *, struct state *);
#endif
