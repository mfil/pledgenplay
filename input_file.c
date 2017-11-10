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

#include <sys/limits.h>

#include <iconv.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "child_errors.h"
#include "child_messages.h"
#include "input_file.h"

#define ID3V2_HEADER_LENGTH 10

static struct input_file {
	FILETYPE type;
	int has_id3v2_tag;
	FILE *file;
} input_file;

static void detect_filetype(void);
static long id3v2_length(char *);
static void detect_id3v2_tag(void);
static void skip_id3v2_tag(void);

NEW_FILE_STATUS
set_new_input_file(int fd)
{
	input_file_close();

	/* Convert the fd to a FILE pointer. */

	input_file.file = fdopen(fd, "rb");
	if (input_file.file == NULL) {
		file_err("fdopen failed");
		return (NEW_FILE_FAILURE);
	}

	detect_id3v2_tag();
	if (!input_file_is_open()) {
		return (NEW_FILE_FAILURE);
	}

	if (input_file_has_id3v2_tag()) {
		skip_id3v2_tag();
	}
	if (!input_file_is_open()) {
		return (NEW_FILE_FAILURE);
	}

	/* Determine the file type. */

	detect_filetype();
	if (input_file.type == NONE) {
		if (input_file_is_open()) {

			/* If the type is NONE and the file is still open,
			 * then there was no error, but the file type was
			 * not recognized. */

			file_errx("failed to determine the file type");
			input_file_close();
			return (NEW_FILE_FAILURE);
		}
		return (NEW_FILE_FAILURE);
	}

	if (input_file_rewind() == SEEK_ERROR) {
		return (NEW_FILE_FAILURE);
	}
	return (NEW_FILE_OK);
}

static void
detect_id3v2_tag(void)
{
	char identifier[3];
	READ_STATUS status = input_file_read(identifier, 3, NULL);
	switch (status) {
	case READ_EOF:
		/* No music file is less than 3 bytes long. */
		input_file_close();
		file_errx("invalid file type");
		/* Fallthrough */
	case READ_ERROR:
	case READ_NO_FILE:
		return;
	default:
		break;
	}

	if (memcmp(identifier, "ID3", 3) == 0) {
		input_file.has_id3v2_tag = 1;
	}
	else {
		input_file.has_id3v2_tag = 0;
	}
	input_file_rewind();
}

static void
skip_id3v2_tag() {
	char id3v2_header[ID3V2_HEADER_LENGTH];
	if (input_file_read(id3v2_header, sizeof(id3v2_header), NULL)
	    != READ_OK) {
		return;
	}
	long length = id3v2_length(id3v2_header);
	if (input_file_seek(length) != SEEK_OK) {
		return;
	}
}

READ_STATUS
input_file_read(void *buf, size_t length, size_t *bytes_read)
{
	if (!input_file_is_open()) {
		return (READ_NO_FILE);
	}

	size_t nread = fread(buf, 1, length, input_file.file);
	if (bytes_read != NULL) {
		*bytes_read = nread;
	}

	READ_STATUS status = READ_OK;

	/* If the return value of fread is smaller than length, it may
	 * indicate an error, or that the end of file is reached, so we
	 * need to figure out which is the case. */

	if (nread < length) {
		if (ferror(input_file.file)) {
			file_err("read failed");
			input_file_close();
			status = READ_ERROR;
		}
		else if (feof(input_file.file)) {
			status = READ_EOF;
		}
	}
	return (status);
}

SEEK_STATUS
input_file_seek(long offset)
{
	if (!input_file_is_open()) {
		return (SEEK_NO_FILE);
	}
	if (fseek(input_file.file, offset, SEEK_CUR)) {
		file_err("seek failed");
		input_file_close();
		return (SEEK_ERROR);
	}
	return (SEEK_OK);
}

SEEK_STATUS
input_file_rewind(void)
{
	if (!input_file_is_open()) {
		return (SEEK_NO_FILE);
	}
	if (fseek(input_file.file, 0, SEEK_SET)) {
		file_err("rewind failed");
		input_file_close();
		return (SEEK_ERROR);
	}
	return (SEEK_OK);
}

SEEK_STATUS
input_file_to_eof(void)
{
	if (!input_file_is_open()) {
		return (SEEK_NO_FILE);
	}
	if (fseek(input_file.file, 0, SEEK_END)) {
		file_err("rewind failed");
		input_file_close();
		return (SEEK_ERROR);
	}
	return (SEEK_OK);
}

FILETYPE
input_file_get_type(void)
{
	return (input_file.type);
}

void
input_file_close(void)
{
	if (input_file_is_open()) {
		if (fclose(input_file.file) != 0) {
			child_warn("fclose");
		}
		input_file.file = NULL;
	}
	input_file.type = NONE;
}

int
input_file_has_id3v2_tag(void)
{
	return (input_file.has_id3v2_tag);
}

int
input_file_is_open(void)
{
	return (input_file.file != NULL);
}

static void
detect_filetype(void)
{
	/* Read the file identifier. */

	unsigned char file_id[4];
	if (input_file_read(file_id, 4, NULL) != READ_OK) {
		input_file.type = NONE;
		return;
	}

	if (memcmp(file_id, "fLaC", 4) == 0) {
		input_file.type = FLAC;
		return;
	}
	if (file_id[0] == 0xff && (file_id[1] & 0xf0) == 0xf0) {

		/* If the first 12 bits are 1, it is an MP3 file. */

		input_file.type = MP3;
		return;
	}
	if (memcmp(file_id, "RIFF", 4) == 0) {

		/* If it starts with "RIFF", it might be a WAVE file. */

		if (input_file_seek(4) != SEEK_OK) {
			input_file.type = NONE;
			return;
		}
		unsigned char wave_id[4];
		if (input_file_read(wave_id, 4, NULL) != READ_OK) {
			input_file.type = NONE;
			return;
		}
		if (memcmp(wave_id, "WAVE", 4) != 0) {
			input_file.type = NONE;
			return;
		}
		char chunk_id[4];
		if (input_file_read(chunk_id, 4, NULL) != READ_OK) {
			input_file.type = NONE;
			return;
		}
		if (memcmp(chunk_id, "fmt ", 4) != 0) {
			input_file.type = NONE;
			return;
		}
		if (input_file_seek(4) != SEEK_OK) {
			input_file.type = NONE;
			return;
		}
		const char wave_pcm_tag[2] = {0x01, 0x00};
		char format_tag[2];
		if (input_file_read(format_tag, 2, NULL) != READ_OK) {
			input_file.type = NONE;
			return;
		}
		if (memcmp(format_tag, wave_pcm_tag, 2) != 0) {
			input_file.type = NONE;
			return;
		}
		input_file.type = WAVE_PCM;
		return;
	}
}

static long
id3v2_length(char header[])
{
	return ((header[6] << 21) + (header[7] << 14) + (header[8] << 7));
}
