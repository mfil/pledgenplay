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
#include <string.h>

#include <FLAC/format.h>
#include <FLAC/callback.h>
#include <FLAC/stream_decoder.h>

#include "child_errors.h"
#include "decoder.h"
#include "id3v2.h"
#include "input_file.h"

static FLAC__StreamDecoder *flac_decoder = NULL;
static struct metadata metadata = {NULL, NULL, NULL, NULL, NULL, NULL};

struct flac_client_data {
	int max_samples_per_frame;
	int bits_per_sample;
	int channels;
	struct decoded_frame *frame;
	struct metadata *metadata;
};

struct flac_client_data client_data = {0, 0, 0, NULL, &metadata};

FLAC__StreamDecoderReadStatus flac_read_callback(const FLAC__StreamDecoder *,
    FLAC__byte[], size_t *, void *);
FLAC__StreamDecoderWriteStatus flac_write_callback(const FLAC__StreamDecoder *,
    const FLAC__Frame *, const FLAC__int32 *const [], void *);
void flac_metadata_callback(const FLAC__StreamDecoder *,
    const FLAC__StreamMetadata *, void *);
void flac_error_callback(const FLAC__StreamDecoder *,
    FLAC__StreamDecoderErrorStatus, void *);

static void read_vorbis_comment(const FLAC__StreamMetadata_VorbisComment *,
    struct metadata *);
static void read_stream_info(const FLAC__StreamMetadata_StreamInfo *,
    struct flac_client_data *);
static void assign_metadata(char *, char *, struct metadata *);

DECODER_INIT_STATUS
decoder_initialize(int fd)
{
	if (set_new_input_file(fd) != NEW_FILE_OK) {
		return (DECODER_INIT_FAIL);
	}
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

	/* If no id3v2 tag was found, set up the decoder to look for a
	 * Vorbis Comment. */

	if (!input_file_has_id3v2_tag()) {
		(void)FLAC__stream_decoder_set_metadata_respond(flac_decoder,
		    FLAC__METADATA_TYPE_VORBIS_COMMENT);
	}

	/* Initialize the decoder instance with callbacks. */

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

DECODER_DECODE_STATUS
decoder_decode_next_frame(struct decoded_frame **next_frame)
{
	if (! FLAC__stream_decoder_process_single(flac_decoder)) {
		*next_frame = NULL;
		return (DECODER_DECODE_ERROR);
	}
	if (FLAC__stream_decoder_get_state(flac_decoder) ==
	    FLAC__STREAM_DECODER_END_OF_STREAM) {
		*next_frame = NULL;
		return (DECODER_DECODE_FINISHED);
	}
	*next_frame = client_data.frame;
	return (DECODER_DECODE_OK);
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
	/* The decoded audio data is presented as buffer[channel][sample].
	 * Information about the audio data (like number of samples,
	 * bits per sample, etc.) can be found in frame->header. */

	int num_samples = frame->header.blocksize;
	int num_channels = frame->header.channels;
	int bits_per_sample = frame->header.bits_per_sample;
	int bytes_per_sample = bits_per_sample/8;
	size_t decoded_frame_length = (size_t)num_samples *
	    (size_t)num_channels * (size_t)bytes_per_sample;

	/* Allocate the frame. */

	struct decoded_frame *decoded_frame =
	    malloc(sizeof(struct decoded_frame));
	if (decoded_frame == NULL) {
		child_fatal("malloc");
	}
	decoded_frame->data = malloc(decoded_frame_length);
	if (decoded_frame->data == NULL) {
		child_fatal("malloc");
	}
	decoded_frame->length = decoded_frame_length;
	decoded_frame->samples = num_samples;
	decoded_frame->bits_per_sample = bits_per_sample;
	decoded_frame->channels = num_channels;

	/* Copy the decoded audio data, interleaving the channels. */

	int sample, channel;
	char *decoded_frame_pos = (char *)decoded_frame->data;
	for (sample = 0; sample < num_samples; sample++) {
		for (channel = 0; channel < num_channels; channel++) {
			memcpy(decoded_frame_pos, &buffer[channel][sample],
			    bytes_per_sample);
			decoded_frame_pos += bytes_per_sample;
		}
	}

	/* Make the frame available to the caller. */

	struct flac_client_data *cdata = (struct flac_client_data *)client_data;
	cdata->frame = decoded_frame;

	return (FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE);
}

void
flac_metadata_callback(const FLAC__StreamDecoder *decoder,
    const FLAC__StreamMetadata *stream_metadata, void *client_data)
{
	struct flac_client_data *cdata = (struct flac_client_data *)client_data;
	if (stream_metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
		read_stream_info(&stream_metadata->data.stream_info, cdata);
	}
	else if (stream_metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
		read_vorbis_comment(&stream_metadata->data.vorbis_comment,
		    cdata->metadata);
	}
}

static void
read_stream_info(const FLAC__StreamMetadata_StreamInfo *stream_info,
    struct flac_client_data *client_data) {
    	
	/* Get parameters of the encoded audio. */

	client_data->max_samples_per_frame = (int)stream_info->max_blocksize;
	client_data->bits_per_sample = (int)stream_info->bits_per_sample;
	client_data->channels = (int)stream_info->channels;

	/* Get the length of the file. */

	unsigned int time_in_secs =
	    stream_info->total_samples/stream_info->sample_rate;
	unsigned int time_in_mins = time_in_secs/60;
	asprintf(&client_data->metadata->time, "%u:%02u", time_in_mins,
	    time_in_secs % 60);
	if (client_data->metadata->time == NULL) {
		child_fatal("asprintf");
	}
}

static void
read_vorbis_comment(FLAC__StreamMetadata_VorbisComment const *vorbis_comment,
    struct metadata *mdata)
{
	int i;
	for (i = 0; i < vorbis_comment->num_comments; i++) {
		const FLAC__StreamMetadata_VorbisComment_Entry *comment;
		comment = &vorbis_comment->comments[i];

		/* The comment is in KEY=VALUE form; extract KEY and
		 * VALUE. */

		char *comment_string = strndup((char *)comment->entry,
		    comment->length);
		if (comment_string == NULL) {
			child_fatal("strndup");
		}
		char *value = comment_string;
		char *key = strsep(&value, "=");

		/* If there was no '=', value is NULL. */

		if (value == NULL) {
			child_warnx("Skipping malformed vorbis comment entry");
		}

		assign_metadata(key, value, mdata);
		free(comment_string);
	}
}

static void
assign_metadata(char *key, char *value, struct metadata *mdata)
{
	
	/* Select the appropriate field (if any) in mdata. */

	char **field = NULL;
	if (strcasecmp(key, "ARTIST") == 0) {
		field = &mdata->artist;
	}
	else if (strcasecmp(key, "TITLE") == 0) {
		field = &mdata->title;
	}
	else if (strcasecmp(key, "ALBUM") == 0) {
		field = &mdata->album;
	}
	else if (strcasecmp(key, "TRACKNUMBER") == 0) {
		field = &mdata->trackno;
	}
	else if (strcasecmp(key, "DATE") == 0) {
		field = &mdata->date;
	}
	if (field == NULL) {
		return;
	}

	/* If the field was already assigned, we warn and free the old
	 * value. Then, assign the value to the field. */

	if (*field != NULL) {
		child_warnx("A metadata field is assigned multiple times.");
		free(*field);
	}
	*field = strdup(value);
	if (*field == NULL) {
		child_fatal("strndup");
	}
}

void
flac_error_callback(const FLAC__StreamDecoder *decoder,
    FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	return;
}

void
free_decoded_frame(struct decoded_frame *frame)
{
	if (frame != NULL) {
		free(frame->data);
	}
	free(frame);
}
