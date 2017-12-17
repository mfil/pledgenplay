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

#include "decoder.h"

typedef enum {OUTPUT_BUSY, OUTPUT_IDLE, OUTPUT_ERROR} OUTPUT_RUN_STATUS;

struct output {
	int (*ready_for_new_frame)(void);
	void (*next_frame)(struct decoded_frame const *);
	OUTPUT_RUN_STATUS (*run)(void);
	nfds_t (*num_pollfds)(void);
	void (*set_pollfds)(struct pollfd *);
	void (*check_pollfds)(struct pollfd *);
};

struct output output_raw(int fd);

#endif
