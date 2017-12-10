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
#include "output.h"

static int fd = -1;
static int ready_to_write = 0;

static OUTPUT_WRITE_STATUS raw_write(void *, size_t, size_t *);
static void run(void);
static void flush(void);
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

	struct output out = { raw_write, run, flush, num_pollfds, set_pollfds,
	    check_pollfds };
	return (out);
}

static OUTPUT_WRITE_STATUS
raw_write(void *buf, size_t bytes, size_t *bytes_written)
{
	if (! ready_to_write) {
		*bytes_written = 0;
		return (OUTPUT_WRITE_OK);
	}

	ssize_t write_rv;
	write_rv = write(fd, buf, bytes);
	if (write_rv < 0) {
		child_warn("write");
		return (OUTPUT_WRITE_ERROR);
	}
	*bytes_written = (size_t)write_rv;
	ready_to_write = 0;
	return (OUTPUT_WRITE_OK);
}

static void
run(void)
{
	return;
}

static void
flush(void)
{
	return;
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
