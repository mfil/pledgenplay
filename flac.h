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

#ifndef PNP_FLAC_H
#define PNP_FLAC_H
#include <FLAC/stream_decoder.h> /* For FLAC__StreamDecoderErrorStatus */

#include "child.h"

struct flac_client_data {
	struct input			*in;
	struct state			*state;
	struct output			*out;
	uint64_t			samples;
	unsigned int			bps, rate, channels;
	int				error;
	FLAC__StreamDecoderErrorStatus	error_status;
	size_t				bytes_written;
};

int	play_flac(struct input *, struct output *, struct state *);
int	extract_meta_flac(struct input *);
#endif
