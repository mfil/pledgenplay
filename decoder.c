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

#include <sys/types.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <FLAC/format.h>
#include <FLAC/callback.h>
#include <FLAC/stream_decoder.h>

#include "child_errors.h"
#include "decoder.h"
#include "id3v2.h"
#include "input_file.h"
#include "output.h"

static OUTPUT_WRITE output_write = NULL;
static FLAC__StreamDecoder *flac_decoder = NULL;
static struct metadata metadata = {NULL, NULL, NULL, NULL, NULL, NULL};

struct flac_client_data {
	struct metadata *metadata;
};

FLAC__StreamDecoderReadStatus flac_read_callback(const FLAC__StreamDecoder *,
    FLAC__byte[], size_t *, void *);
FLAC__StreamDecoderWriteStatus flac_write_callback(const FLAC__StreamDecoder *,
    const FLAC__Frame *, const FLAC__int32 *const [], void *);
void flac_metadata_callback(const FLAC__StreamDecoder *,
    const FLAC__StreamMetadata *, void *);
void flac_error_callback(const FLAC__StreamDecoder *,
    FLAC__StreamDecoderErrorStatus, void *);

DECODER_INIT_STATUS
decoder_initialize(int fd, OUTPUT_WRITE write_function)
{
	if (set_new_input_file(fd) != NEW_FILE_OK) {
		return (DECODER_INIT_FAIL);
	}
	output_write = write_function;

	if (input_file_has_id3v2_tag()) {
		unsigned char header_bytes[ID3V2_HEADER_LENGTH];
		if (input_file_read(header_bytes, sizeof(header_bytes), NULL) !=
		    READ_OK) {
			return (DECODER_INIT_FAIL);
		}
		struct id3v2_header header = parse_id3v2_header(header_bytes);

		unsigned char *tag_bytes = malloc(header.tag_length);
		if (tag_bytes == NULL) {
			child_fatal("malloc");
		}
		if (input_file_read(tag_bytes, header.tag_length, NULL) !=
		    READ_OK) {
			return (DECODER_INIT_FAIL);
		}
		if (parse_id3v2_tag(tag_bytes, header, &metadata) !=
		    PARSE_ID3V2_OK) {
			child_warn("Error parsing file metadata.");
		}
		free(tag_bytes);
	}

	if (input_file_get_type() != FLAC) {
		child_fatal("Only FLAC files are supported for now.");
	}

	/* Create new FLAC decoder instance if necessary. */

	if (flac_decoder != NULL) {
		if (FLAC__stream_decoder_get_state(flac_decoder) !=
		    FLAC__STREAM_DECODER_UNINITIALIZED) {
			child_warn("FLAC decoder was not finalized.");
			(void)FLAC__stream_decoder_finish(flac_decoder);
		}
	}
	else {
		flac_decoder = FLAC__stream_decoder_new();
		if (flac_decoder == NULL) {
			child_fatal("Out of memory.");
		}
	}

	/* Initialize the decoder instance with callbacks. */

	struct flac_client_data client_data = { &metadata };
	FLAC__stream_decoder_init_stream(flac_decoder,
	    flac_read_callback,
	    NULL, /* Seek */
	    NULL, /* Tell */
	    NULL, /* Length */
	    NULL, /* EOF */
	    flac_write_callback,
	    flac_metadata_callback,
	    flac_error_callback,
	    &client_data);

	/* Process metadata. */

	FLAC__stream_decoder_process_until_end_of_metadata(flac_decoder);

	return (DECODER_INIT_OK);
}

struct metadata const *
decoder_get_metadata(void)
{
	return (&metadata);
}

FLAC__StreamDecoderReadStatus
flac_read_callback(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[],
    size_t *bytes, void *client_data)
{
	/* Read the bytes from the input file. */

	size_t bytes_read = 0;
	READ_STATUS status = input_file_read(buffer, *bytes, &bytes_read);
	*bytes = bytes_read;

	/* Determine the return code. Note that the end of stream should
	 * only be signalled if there were no bytes read. */

	if (status == READ_OK) {
		return (FLAC__STREAM_DECODER_READ_STATUS_CONTINUE);
	}
	else if (status == READ_EOF) {
		if (bytes_read == 0) {
			return (FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM);
		}
		else {
			/* Return CONTINUE, END_OF_STREAM will be returned
			 * in the next call when 0 bytes are read. */

			return (FLAC__STREAM_DECODER_READ_STATUS_CONTINUE);
		}
	}
	else {
		return (FLAC__STREAM_DECODER_READ_STATUS_ABORT);
	}
}

FLAC__StreamDecoderWriteStatus
flac_write_callback(const FLAC__StreamDecoder *decoder,
    const FLAC__Frame *frame, const FLAC__int32 *const buffer[],
    void *client_data)
{
	return (FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE);
}

void
flac_metadata_callback(const FLAC__StreamDecoder *decoder,
    const FLAC__StreamMetadata *stream_metadata, void *client_data)
{
	struct flac_client_data *cdata = (struct flac_client_data *)client_data;
	if (stream_metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
		unsigned int time_in_secs =
		    stream_metadata->data.stream_info.total_samples/
		    stream_metadata->data.stream_info.sample_rate;
		unsigned int time_in_mins = time_in_secs/60;
		asprintf(&cdata->metadata->time, "%u:%02u", time_in_mins,
		    time_in_secs % 60);
		if (cdata->metadata->time == NULL) {
			child_fatal("asprintf");
		}
	}
	else if (stream_metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
	}
}

void
flac_error_callback(const FLAC__StreamDecoder *decoder,
    FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	return;
}
