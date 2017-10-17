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
#include "file.h"

static MESSAGE_TYPE	vorbis_to_type(char *);
static int		id3v2_to_type(unsigned char *);
static size_t		be_to_uint(unsigned char *);
static size_t		le_to_uint(unsigned char *);
static unsigned char	*uint_to_le(unsigned int, unsigned char *);

int
filetype(int fd)
{
	ssize_t		nread;
	unsigned char	buf[22];
	const char	wave_pcm_tag[2] = {0x01, 0x00};
	off_t		tag_size;

	nread = read(fd, buf, sizeof(buf));
	if (nread < 0)
		return (-1);
	/* skip ID3v2 tag, if present. */
	if (nread >= 3 && memcmp(buf, "ID3", 3) == 0) {
		if (nread <= 10)
			return (UNKNOWN);
		tag_size = (buf[6] << 21) + (buf[7] << 14) + (buf[8] << 7)
		    + buf[9];
		if (lseek(fd, tag_size+10, SEEK_SET) < 0)
			return (-1);
		nread = read(fd, buf, sizeof(buf));
		if (nread < 0)
			return (-1);
	}
	if (lseek(fd, 0, SEEK_SET) != 0)
		return (-1);
	if (nread >= 2 && buf[0] == 0xff && (buf[1] & 0xe0) == 0xe0)
		/* mp3 framesync: 12 bits set to 1. */
		return (MP3);
	if (nread >= 4 && memcmp(buf, "fLaC", 4) == 0)
		return (FLAC); if (nread == 22 && memcmp(buf, "RIFF", 4) == 0 &&
	    memcmp(buf+8, "WAVE", 4) == 0 &&
	    memcmp(buf+12, "fmt ", 4) == 0 &&
	    memcmp(buf+20, wave_pcm_tag, sizeof(wave_pcm_tag)) == 0)
		return (WAVE_PCM);
	return (UNKNOWN);
}

int
parse_vorbis_comment(unsigned char *vcm, ssize_t len)
{
	unsigned char	*key;
	size_t		ncomm, comm_len, key_len, i;
	int		type;

	if (len <= 8)
		return (-1); /* Too short. */
	/* Skip vendor tag. */
	comm_len = le_to_uint(vcm);
	len -= comm_len + 4;
	if (len < 0)
		return (-1);
	vcm += comm_len + 4;
	ncomm = le_to_uint(vcm); /* Number of comments */
	len -= 4;
	if (len < 0)
		return (-1);
	vcm += 4;
	/* Parse the metadata. */
	for (i = 0; i < ncomm; i++) {
		comm_len = le_to_uint(vcm); /* Comment length */
		len -= 4 + comm_len;
		if (len < 0)
			return (-1);
		vcm += 4;
		/*
		 * The comment is of the form key=value. Replace the '=' by a
		 * '\0' to make key a string.
		 */
		key = vcm;
		while (*vcm != '=' && vcm++ < key + comm_len)
			;
		*vcm++ = '\0';
		if ((key_len = vcm - key) < comm_len) {
			comm_len -= key_len;
			/* Send the message to the parent. */
			type = vorbis_to_type((char *)key);
			if (type > -1) {
				char *string = malloc(comm_len + 1);
				if (string == NULL) {
					child_fatal("malloc");
				}
				strlcpy(string, vcm, comm_len + 1);
				enqueue_message(type, string);
				free(string);
			}
		}
		else
			/* Malformed comment. */
			return (-1);
		vcm += comm_len;
	}
	return (0);
}

int
parse_id3v2(unsigned char flags, unsigned char *id3, ssize_t len)
{
	size_t		framelen, trcklen, utf8len, tlen;
	unsigned char	*frameflags;
	char 		*utf8str, *utf8strp, *tstr;
	const char	*errstr;
	int		type;
	long long	time;
	iconv_t		conv;

	/* Skip extended header, if present. */
	if (flags & 0x40) {
		if (len < 4)
			return (-1);
		framelen = be_to_uint(id3);
		len -= framelen + 4;
		if (len < 0)
			return (-1);
		id3 += framelen + 4;
	}
	while (len > 0) {
		if (*id3 == '\0')
			return (0); /* The rest of the tag is padding. */
		len -= ID3_HDR_LEN; /* ID3_HDR_LEN = 4 + 4 + 2 */
		if (len < 0)
			return (-1);
		type = id3v2_to_type(id3);
		id3 += 4;
		framelen = be_to_uint(id3);
		id3 += 4;
		frameflags = id3;
		id3 += 2;
		len -= framelen;
		if (len < 0)
			return (-1);
		if (type == -1) {
			/* We don't care about this frame. */
			id3 += framelen;
			continue;
		}
		if (frameflags[1] & 0x40) {
			/* Encrypted frame. */
			child_warnx("Encrypted id3v2 frames not supported.");
			id3 += framelen;
			continue;
		}
		if (frameflags[1] & 0x80) {
			/* Compressed frame. */
			child_warnx("Compressed id3v2 frames not supported.");
			id3 += framelen;
			continue;
		}
		if (framelen < 1)
			return (-1);
		if (*id3 == 0x00) {
			/* ISO-8859-1 encoding */
			utf8len = framelen;
			conv = iconv_open("UTF-8", "ISO-8859-1");
		}
		else if (*id3 == 0x01) {
			/* UTF-16 encoding */
			utf8len = framelen/2 + 1;
			conv = iconv_open("UTF-8", "UTF-16");
		}
		else {
			/* Invalid */
			child_warnx("Unknown text encoding in id3v2 frame.");
			return (-1);
		}
		id3++;
		framelen--;
		if ((utf8str = malloc(utf8len)) == NULL)
			child_fatal("malloc");
		utf8strp = utf8str;
		if (iconv(conv, (char **)&id3, &framelen, &utf8strp, &utf8len)
		    == -1) {
			child_warn("iconv");
			free(utf8str);
			return (-1);
		}
		*utf8strp = '\0';
		if (type == META_TIME) {
			/* The time is given in milliseconds. */
			time = strtonum(utf8str, 0, INT_MAX, &errstr);
			if (errstr == NULL) {
				child_warn("strtonum");
				return (-1);
			}
			time = time % 1000 < 500 ? time/1000 : time/1000 + 1;
			tlen = asprintf(&tstr, "%d:%02d", (int)time/60,
			    (int)time % 60);
			if (tstr == NULL)
				child_fatal("malloc");
			enqueue_message(META_TIME, tstr);
			free(tstr);
		}
		else if (type == META_TRACKNO) {
			for (trcklen = 0; trcklen < strlen(utf8str);
			    trcklen++) {
				if (utf8str[trcklen] == '/')
					break;
				}
			enqueue_message(type, utf8str);
		}
		else
			enqueue_message(type, utf8str);
		free(utf8str);
		id3 += framelen;
	}
	return (0);
}

static int
id3v2_to_type(unsigned char *id)
{
	if (memcmp(id, "TLEN", 4) == 0)
		return (META_TIME);
	if (memcmp(id, "TPE1", 4) == 0)
		return (META_ARTIST);
	if (memcmp(id, "TIT2", 4) == 0)
		return (META_TITLE);
	if (memcmp(id, "TALB", 4) == 0)
		return (META_ALBUM);
	if (memcmp(id, "TRCK", 4) == 0)
		return (META_TRACKNO);
	if (memcmp(id, "TYER", 4) == 0)
		return (META_DATE);
	return (-1);
}


/* Convert a VORBIS COMMENT key to the appropriate imsg type. */
static MESSAGE_TYPE
vorbis_to_type(char *key)
{
	if (strcasecmp(key, "ARTIST") == 0) 
		return (META_ARTIST);
	else if (strcasecmp(key, "TITLE") == 0)
		return (META_TITLE);
	else if (strcasecmp(key, "ALBUM") == 0)
		return (META_ALBUM);
	else if (strcasecmp(key, "TRACKNUMBER") == 0)
		return (META_TRACKNO);
	else if (strcasecmp(key, "DATE") == 0)
		return (META_DATE);
	return (-1);
}

int
write_wav_header(FILE *f, unsigned int channels, unsigned int rate,
    unsigned int bps, uint64_t samples)
{
	unsigned char	buf[4];
	uint64_t	data_size;
	size_t		bytes;

	if (bps % 8 != 0)
		return (-1);
	data_size = samples*channels*bps/8;
	if (samples > UINT32_MAX || data_size > UINT32_MAX)
		/* The file is too large for the WAVE format. */
		return (-1);
	bytes = fwrite("RIFF", 1, 4, f);
	if (bytes < 4)
		return (-1);
	/* Write the file size (excluding the RIFF tag and the size). */
	bytes += fwrite(uint_to_le(data_size + 36, buf), 1, 4, f);
	if (bytes < 8)
		return (-1);
	bytes += fwrite("WAVEfmt \x10\x00\x00\x00\x01\x00", 1, 14, f);
	/*
	 * Bytes 9-15 are the size of the fmt chunk (16 bytes).
	 * The last two bytes are the tag for the WAVE PCM format.
	 */
	if (bytes < 22)
		return (-1);
	bytes += fwrite(uint_to_le(channels, buf), 1, 2, f);
	if (bytes < 24)
		return (-1);
	bytes += fwrite(uint_to_le(rate, buf), 1, 4, f);
	if (bytes < 28)
		return (-1);
	bytes += fwrite(uint_to_le(rate*channels*bps/8, buf), 1, 4, f);
	/* Data rate (bytes/s) */
	if (bytes < 32)
		return (-1);
	bytes += fwrite(uint_to_le(channels*bps/8, buf), 1, 2, f);
	/* Block size in bytes. (Block = One sample per channel) */
	if (bytes < 34)
		return (-1);
	bytes += fwrite(uint_to_le(bps, buf), 1, 2, f);
	if (bytes < 36)
		return (-1);
	bytes += fwrite("data", 1, 4, f);
	if (bytes < 40)
		return (-1);
	bytes += fwrite(uint_to_le(data_size, buf), 1, 4, f);
	if (bytes < 44)
		return (-1);
	return ((int)bytes);
}

/* Decode/encode big and little endian */
static size_t
be_to_uint(unsigned char *d)
{
	return ((d[0] << 24) + (d[1] << 16) + (d[2] << 8) + d[3]);
}

static size_t
le_to_uint(unsigned char *d)
{
	return (d[0] + (d[1] << 8) + (d[2] << 16) + (d[3] << 24));
}

static unsigned char *
uint_to_le(unsigned int n, unsigned char *buf)
{
	/* buf must be able to hold four bytes. */
	int 	i;

	for (i = 0; i < 4; i++) {
		buf[i] = n % (1 << 8);
		n >>= 8;
	}
	return (buf);
}
