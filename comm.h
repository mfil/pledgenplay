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

#ifndef PNP_COMM_H
#define PNP_COMM_H
#include "child.h"

enum	{NEW_FILE,	/* Send file descriptor to child */
	 CMD_EXIT,	/* Tell child to exit */
	 CMD_META,	/* Tell child to extract metadata */
	 CMD_PLAY,	/* Tell child to start playback */
	 CMD_PAUSE,	/* Tell child to pause playback */
	 MSG_ACK,	/* Acknowledge */
	 MSG_NACK,
	 MSG_FILE_ERR,	/* Error reading file */
	 MSG_DONE,	/* Playback/decoding finished */
	 MSG_WARN,	/* Warning */
	 MSG_FATAL,	/* Fatal error; child process exits */
	 META_ARTIST,	/* Child sends metadata */
	 META_TITLE,
	 META_ALBUM,
	 META_TRACKNO,
	 META_DATE,
	 META_TIME};

void		msg(int, void *, size_t);
void		msgstr(int, char *);
void		msgwarn(char *);
void		msgwarnx(char *);
void		file_err(struct input *, char *);
void		file_errx(struct input *, char *);
__dead void	fatal(char *);
__dead void	fatalx(char *);
__dead void	_err(char *);
void		comm_init(int);

#endif
