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

#ifndef PNP_INPUT_FILE_H
#define PNP_INPUT_FILE_H

typedef enum {NONE, FLAC, MP3, WAVE_PCM} FILETYPE;
typedef enum {
	READ_OK,
	READ_EOF,
	READ_ERROR,
	READ_NO_FILE
} READ_STATUS;
typedef enum {SEEK_OK, SEEK_ERROR, SEEK_NO_FILE} SEEK_STATUS;
typedef enum {
	NEW_FILE_OK,
	NEW_FILE_FAILURE,
} NEW_FILE_STATUS;

NEW_FILE_STATUS set_new_input_file(int);
void input_file_close(void);
READ_STATUS input_file_read(void *, size_t, size_t *);
SEEK_STATUS input_file_seek(long offset);
SEEK_STATUS input_file_rewind(void);
SEEK_STATUS input_file_to_eof(void);
FILETYPE input_file_get_type(void);
int input_file_has_id3v2_tag(void);
int input_file_is_open(void);

#endif
