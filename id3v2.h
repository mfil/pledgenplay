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

#ifndef PNP_ID3V2_H
#define PNP_ID3V2_H

#include <stdint.h>

#include "decoder.h"

#define ID3V2_HEADER_LENGTH 10

struct id3v2_header {
	uint32_t tag_length;
	int extended_header;
};

typedef enum { PARSE_ID3V2_OK, PARSE_ID3V2_ERROR } PARSE_ID3V2_STATUS;

struct id3v2_header parse_id3v2_header(unsigned char *);
PARSE_ID3V2_STATUS parse_id3v2_tag(void *, struct id3v2_header,
    struct metadata *);

#endif
