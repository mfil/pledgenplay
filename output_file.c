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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "child_errors.h"
#include "decoder.h"
#include "output.h"

static int fd = -1;
static int ready_to_write = 0;
static struct decoded_frame const *current_frame = NULL;
static char const *position_in_frame = NULL;
static size_t bytes_in_file = 0;
static size_t bytes_left_in_frame = 0;

const int wav_header_length = 44;

static int ready_for_new_frame(void);
static void next_frame(struct decoded_frame const *);
static OUTPUT_RUN_STATUS run(void);
static nfds_t num_pollfds(void);
static void set_pollfds(struct pollfd *);
static void check_pollfds(struct pollfd *);
static void close_raw(void);
static void close_wav(void);
static void *wav_header(struct audio_parameters const *);

OUTPUT_INIT_STATUS
output_raw(int new_fd, struct output *out)
{
	if (fd != -1) {
		close(fd);
	}
	fd = new_fd;
	current_frame = NULL;
	position_in_frame = NULL;
	bytes_left_in_frame = 0;

	out->ready_for_new_frame = ready_for_new_frame;
	out->next_frame = next_frame;
	out->run = run;
	out->num_pollfds = num_pollfds;
	out->set_pollfds = set_pollfds;
	out->check_pollfds = check_pollfds;
	out->close = close_raw;

	bytes_in_file = 0;
	return (OUTPUT_INIT_OK);
}

OUTPUT_INIT_STATUS
output_wav(int new_fd, struct audio_parameters const *params,
    struct output *out)
{
	if (fd != -1) {
		close(fd);
	}
	fd = new_fd;
	current_frame = NULL;
	position_in_frame = NULL;
	bytes_left_in_frame = 0;

	ssize_t bytes_written = write(fd, wav_header(params),
	    wav_header_length);
	if (bytes_written < wav_header_length) {
		return (OUTPUT_INIT_ERROR);
	}
	bytes_in_file = wav_header_length;

	out->ready_for_new_frame = ready_for_new_frame;
	out->next_frame = next_frame;
	out->run = run;
	out->num_pollfds = num_pollfds;
	out->set_pollfds = set_pollfds;
	out->check_pollfds = check_pollfds;
	out->close = close_wav;

	return (OUTPUT_INIT_OK);
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

static void
close_raw(void)
{
	if (close(fd) == -1) {
		child_warn("error closing output file");
	}
	fd = -1;
}

static void
close_wav(void)
{
	/* Pad the file to even length. */

	if (bytes_in_file % 2 != 0) {
		if (write(fd, "\x00", 1) < 1) {
			child_warn("error closing output file");
		}
	}

	if (close(fd) == -1) {
		child_warn("error closing output file");
	}
	fd = -1;
}

static void *
wav_header(struct audio_parameters const *params)
{
	char *wav_header = malloc(wav_header_length);
	if (wav_header == NULL) {
		child_fatal("malloc");
	}

	/* Calculate the length of the file, excluding the header. */

	size_t data_length = (size_t)params->channels *
	    (size_t)(params->bits_per_sample/8) * params->total_samples;
	if (data_length > UINT32_MAX) {
		child_fatalx("File is too large for the WAVE format.");
	}

	const char *riff_tag = "RIFF";
	memcpy(wav_header, "RIFF", strlen(riff_tag));

	/* Write the file size in little-endian, including the header,
	 * except for the RIFF tag and the length field we're writing
	 * right now. */

	size_t file_length = data_length + wav_header_length - 8;
	int i;
	for (i = 0; i < 4; i++) {
		wav_header[4+i] = (file_length >> 8*i) & 0xff;
	}


	/* Bytes 9-15 are the size of the fmt chunk (16 bytes).
	 * The last two bytes are the tag for the WAVE PCM format. */

	const char *boilerplate = "WAVEfmt \x10\x00\x00\x00\x01\x00";
	memcpy(wav_header + 8, boilerplate, 14);

	/* Number of channels in 2 bytes, little-endian. */

	for (i = 0; i < 2; i++) {
		wav_header[22+i] = (params->channels >> 8*i) & 0xff;
	}

	/* Rate in 4 bytes, little-endian. */

	for (i = 0; i < 4; i++) {
		wav_header[24+i] = (params->rate >> 8*i) & 0xff;
	}


	/* Data rate (bytes/s) */

	uint32_t data_rate = (uint32_t)params->rate *
	    (uint32_t)params->channels * (size_t)(params->bits_per_sample/8);
	for (i = 0; i < 4; i++) {
		wav_header[28+i] = (data_rate >> 8*i) & 0xff;
	}

	/* Block size in bytes. (Block = One sample per channel) */

	unsigned int block_size = params->channels * params->bits_per_sample/8;
	for (i = 0; i < 2; i++) {
		wav_header[32+i] = (block_size >> 8*i) & 0xff;
	}

	/* Bits per sample. */

	for (i = 0; i < 2; i++) {
		wav_header[34+i] = (params->bits_per_sample >> 8*i) & 0xff;
	}

	memcpy(wav_header+36, "data", 4);

	/* Write the data length. */

	for (i = 0; i < 4; i++) {
		wav_header[40+i] = (data_length >> 8*i) & 0xff;
	}

	return (wav_header);
}