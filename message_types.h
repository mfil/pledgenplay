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

#ifndef PNP_MESSAGE_TYPES_H
#define PNP_MESSAGE_TYPES_H

/* Command message types from parent to child. */

typedef enum {
	CMD_SET_INPUT,
	CMD_SET_OUTPUT_FILE_RAW,
	CMD_SET_OUTPUT_FILE_WAV,
	CMD_SET_OUTPUT_SNDIO,
	CMD_EXIT,		/* Tell child to exit */
	CMD_META,		/* Tell child to extract metadata */
	CMD_PLAY,		/* Tell child to start playback */
	CMD_PAUSE,		/* Tell child to pause playback */
	CMD_MESSAGE_SENTINEL,
} CMD_MESSAGE_TYPE;

/* Messages from child to parent. */
typedef enum {
	MSG_HELLO,		/* Startup */
	MSG_DONE,		/* Playback/decoding finished */
	MSG_WARN,
	MSG_INPUT_ERROR,
	MSG_OUTPUT_ERROR,
	MSG_FATAL,		/* Fatal error; child process exits */
	META_ARTIST,		/* Child sends metadata */
	META_TITLE,
	META_ALBUM,
	META_TRACKNO,
	META_DATE,
	META_TIME,
	MSG_SENTINEL,
} MESSAGE_TYPE;


#endif
