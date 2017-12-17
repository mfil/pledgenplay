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

#ifndef PNP_DECODER_H
#define PNP_DECODER_H

#include <sys/types.h>

typedef enum {
	DECODER_INIT_OK,
	DECODER_INIT_FAIL
} DECODER_INIT_STATUS;

typedef enum {
	DECODER_DECODE_OK,
	DECODER_DECODE_FINISHED,
	DECODER_DECODE_ERROR
} DECODER_DECODE_STATUS;

struct metadata {
	char *artist;
	char *title;
	char *album;
	char *date;
	char *time;
	char* trackno;
};

struct decoded_frame {
	void *data;
	size_t length;
	int samples;
	int bits_per_sample;
	int channels;
};

DECODER_INIT_STATUS decoder_initialize(int);
DECODER_DECODE_STATUS decoder_decode_next_frame(struct decoded_frame **);
struct metadata const *decoder_get_metadata(void);
void free_decoded_frame(struct decoded_frame *);

#endif
