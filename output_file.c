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

#include <poll.h>

#include <unistd.h>

#include "child_errors.h"
#include "decoder.h"
#include "output.h"

static int fd = -1;
static int ready_to_write = 0;
static struct decoded_frame const *current_frame = NULL;
static char const *position_in_frame = NULL;
static size_t bytes_left_in_frame = 0;

static int ready_for_new_frame(void);
static void next_frame(struct decoded_frame const *);
static OUTPUT_RUN_STATUS run(void);
static nfds_t num_pollfds(void);
static void set_pollfds(struct pollfd *);
static void check_pollfds(struct pollfd *);

struct output
output_raw(int new_fd)
{
	if (fd != -1) {
		close(fd);
	}
	fd = new_fd;
	current_frame = NULL;
	position_in_frame = NULL;
	bytes_left_in_frame = 0;

	struct output out = { ready_for_new_frame, next_frame, run,
	    num_pollfds, set_pollfds, check_pollfds };
	return (out);
}

static int
ready_for_new_frame(void)
{
	return (bytes_left_in_frame == 0);
}

void
next_frame(struct decoded_frame const *frame)
{
	current_frame = frame;
	bytes_left_in_frame = frame->length;
	position_in_frame = frame->data;
}

static OUTPUT_RUN_STATUS
run(void)
{
	if (ready_to_write && bytes_left_in_frame > 0) {
		ssize_t bytes_written = write(fd, position_in_frame,
		    bytes_left_in_frame);
		if (bytes_written < 0) {
			return (OUTPUT_ERROR);
		}
		bytes_left_in_frame -= (size_t)bytes_written;
		position_in_frame += bytes_written;
	}
	ready_to_write = 0;

	if (bytes_left_in_frame > 0) {
		return (OUTPUT_BUSY);
	}
	else {
		return (OUTPUT_IDLE);
	}
}

static nfds_t
num_pollfds(void)
{
	return (1);
}

static void
set_pollfds(struct pollfd *pfd)
{
	pfd->fd = fd;
	pfd->events = POLLOUT;
}

static void
check_pollfds(struct pollfd *pfd)
{
	if (pfd->revents & POLLOUT) {
		ready_to_write = 1;
	}
	else {
		ready_to_write = 0;
	}
	return;
}
