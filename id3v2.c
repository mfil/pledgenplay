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

#include <sys/limits.h>
#include <sys/types.h>

#include <iconv.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "child_errors.h"
#include "decoder.h"
#include "id3v2.h"

typedef enum {
	FRAME_ID_TIME,
	FRAME_ID_ARTIST,
	FRAME_ID_TITLE,
	FRAME_ID_ALBUM,
	FRAME_ID_TRACKNO,
	FRAME_ID_DATE,
	FRAME_ID_OTHER,
} FRAME_ID;

struct frame_header {
	FRAME_ID frame_id;
	uint32_t length;
	int compressed;
	int encrypted;
};

static struct frame_header parse_frame_header(unsigned char *);
static size_t size_t_from_big_endian(unsigned char *);
static FRAME_ID determine_frame_id(unsigned char *);
static void parse_frame(struct frame_header *const, unsigned char *,
    struct metadata *);

struct id3v2_header
parse_id3v2_header(unsigned char *header)
{
	unsigned char *hdr = (unsigned char *)header;
	struct id3v2_header parsed_header;

	/* Check for the extended header flag. */

	if ((hdr[5] & 0x40)) {
		parsed_header.extended_header = 1;
	}
	else {
		parsed_header.extended_header = 0;
	}

	/* Decode the tag length. The length is stored in bytes 6, 7, 8,
	 * and 9, but only the last 7 bits in each byte are used.
	 * Byte 6 stores the 7 most significant bits, and byte 9 the
	 * least significant 7 bits. */

	parsed_header.tag_length = (header[6] << 21) + (header[7] << 14) +
	    (header[8] << 7) + header[9];

	return (parsed_header);
}

PARSE_ID3V2_STATUS
parse_id3v2_tag(void *id3v2_tag, struct id3v2_header header,
    struct metadata *mdata)
{
	size_t remaining_length = header.tag_length;
	unsigned char *position = (unsigned char *)id3v2_tag;

	/* Skip the extended header, if present. */

	if (header.extended_header) {
		
		/* The first four bytes are the extended header length. */

		size_t ext_header_length = size_t_from_big_endian(position);
		if (remaining_length < ext_header_length) {
			return (PARSE_ID3V2_ERROR);
		}
		remaining_length -= ext_header_length;
		position += ext_header_length;
	}

	/* Parse the id3v2 frames. */

	const size_t FRAME_HEADER_SIZE = 10;
	while (FRAME_HEADER_SIZE < remaining_length /*&& *position != '\0'*/) {

		/* Parse the frame header. */

		struct frame_header frame_header;
		frame_header = parse_frame_header(position);
		remaining_length -= FRAME_HEADER_SIZE;
		position += FRAME_HEADER_SIZE;

		/* Check that the supposed length of the frame is not
		 * larger than the remaining tag. */

		if (remaining_length < frame_header.length) {
			return (PARSE_ID3V2_ERROR);
		}

		/* Parse the frame. */

		parse_frame(&frame_header, position, mdata);
		remaining_length -= frame_header.length;
		position += frame_header.length;
	}
	return (PARSE_ID3V2_OK);
}

static struct frame_header
parse_frame_header(unsigned char *bytes)
{
	struct frame_header header;

	header.frame_id = determine_frame_id(bytes);

	/* Determine the frame length. */

	header.length = size_t_from_big_endian(bytes + 4);

	/* Detect relevant flags. */

	if (bytes[9] & 0x06) {
		header.encrypted = 1;
	}
	else {
		header.encrypted = 0;
	}
	if (bytes[9] & 0x08) {
		header.compressed = 1;
	}
	else {
		header.compressed = 0;
	}
	return (header);
}

static void
parse_frame(struct frame_header *const header, unsigned char *const frame,
    struct metadata *mdata)
{

	/* Select the metadata field that we need to fill in. */

	char **field;
	switch (header->frame_id) {
	case (FRAME_ID_TIME):
		field = &mdata->time;
		break;
	case (FRAME_ID_ARTIST):
		field = &mdata->artist;
		break;
	case (FRAME_ID_TITLE):
		field = &mdata->title;
		break;
	case (FRAME_ID_ALBUM):
		field = &mdata->album;
		break;
	case (FRAME_ID_TRACKNO):
		field = &mdata->trackno;
		break;
	case (FRAME_ID_DATE):
		field = &mdata->date;
		break;
	default:
		field = NULL;
	}

	if (field == NULL || header->length == 0) {
		return;
	}

	/* If the frame is compressed or encrypted, warn
	 * and ignore it. */

	if (header->compressed || header->encrypted) {
		child_warnx("Compressed or encrypted frames are unsupported.");
		return;
	}

	/* Warn if the field is being reassigned. */

	if (*field != NULL) {
		child_warnx("Duplicate metadata.");
		free(*field);
	}

	/* Convert the frame content to utf8. */

	/* Find out how the frame is encoded and what the length of the
	 * utf8 string will be. */

	size_t utf8_len;
	iconv_t conv;
	if (frame[0] == 0x00) {
		/* ISO-8859-1 encoding */
		utf8_len = header->length - 1;
		conv = iconv_open("UTF-8", "ISO-8859-1");
	}
	else if (frame[0] == 0x01) {
		/* UTF-16 encoding */
		utf8_len = (header->length - 1)/2;
		conv = iconv_open("UTF-8", "UTF-16");
	}
	else {
		child_warn("Invalid encoding, skipping frame.");
		return;
	}

	/* Convert the string. */

	char *utf8_str = malloc(utf8_len);
	if (utf8_str == NULL) {
		child_fatal("malloc");
	}
	char *iconv_input = (char *)frame + 1;
	size_t input_length = header->length - 1;
	char *iconv_output = utf8_str; /* We need a copy of this pointer
	                                * because iconv changes it. */
	iconv(conv, &iconv_input, &input_length, &iconv_output, &utf8_len);

	/* Copy the metadata information. If the information in question
	 * is the length, we convert it from milliseconds to "mm:ss". */

	if (header->frame_id == FRAME_ID_TIME) {
		const char *err_str;
		int time_in_ms = (int)strtonum(utf8_str, 0, INT_MAX, &err_str);
		free(utf8_str);
		if (err_str != NULL) {
			child_warn("invalid time");
			return;
		}
		int time_in_s = time_in_ms/1000;
		int minutes = time_in_s/60;
		int seconds = time_in_s % 60;
		char *time_string;
		(void)asprintf(&time_string, "%d:%02d", minutes, seconds);
		if (time_string == NULL)
			child_fatal("asprintf");
		*field = time_string;
	}
	else {
		*field = utf8_str;
	}
}

static FRAME_ID
determine_frame_id(unsigned char *id)
{
	if (memcmp(id, "TLEN", 4) == 0)
		return (FRAME_ID_TIME);
	if (memcmp(id, "TPE1", 4) == 0)
		return (FRAME_ID_ARTIST);
	if (memcmp(id, "TIT2", 4) == 0)
		return (FRAME_ID_TITLE);
	if (memcmp(id, "TALB", 4) == 0)
		return (FRAME_ID_ALBUM);
	if (memcmp(id, "TRCK", 4) == 0)
		return (FRAME_ID_TRACKNO);
	if (memcmp(id, "TYER", 4) == 0)
		return (FRAME_ID_DATE);
	return (FRAME_ID_OTHER);
}

static size_t
size_t_from_big_endian(unsigned char *bytes)
{
	return ((bytes[0] << 24) + (bytes[1] << 16) + (bytes[2] << 8) +
	    bytes[3]);
}
