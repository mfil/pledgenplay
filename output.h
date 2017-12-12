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

#ifndef PNP_OUTPUT_H
#define PNP_OUTPUT_H

#include <sys/types.h>

#include <poll.h>

typedef enum {OUTPUT_WRITE_OK, OUTPUT_WRITE_ERROR} OUTPUT_WRITE_STATUS;

typedef OUTPUT_WRITE_STATUS (*OUTPUT_WRITE)(void *, size_t, size_t *);

struct output {
	OUTPUT_WRITE_STATUS (*write)(const void *, size_t, size_t *);
	void (*run)(void);
	void (*flush)(void);
	nfds_t (*num_pollfds)(void);
	void (*set_pollfds)(struct pollfd *);
	void (*check_pollfds)(struct pollfd *);
};

struct output output_raw(int fd);

#endif
