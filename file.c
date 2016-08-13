#include <sys/limits.h>

#include <iconv.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "file.h"

static int	vorbis_to_type(char *);
static int	id3v2_to_type(unsigned char *);
static size_t	be_to_uint(unsigned char *);
static size_t	le_to_uint(unsigned char *);

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
		return (FLAC);
	if (nread == 22 && memcmp(buf, "RIFF", 4) == 0 &&
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
			if (type > -1)
				msg(type, vcm, comm_len);
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
			msgwarnx("Encrypted id3v2 frames not supported.");
			id3 += framelen;
			continue;
		}
		if (frameflags[1] & 0x80) {
			/* Compressed frame. */
			msgwarnx("Compressed id3v2 frames not supported.");
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
			msgwarnx("Unknown text encoding in id3v2 frame.");
			return (-1);
		}
		id3++;
		framelen--;
		if ((utf8str = malloc(utf8len)) == NULL)
			fatal("malloc");
		utf8strp = utf8str;
		if (iconv(conv, (char **)&id3, &framelen, &utf8strp, &utf8len)
		    == -1) {
			msgwarn("iconv");
			free(utf8str);
			return (-1);
		}
		*utf8strp = '\0';
		if (type == META_TIME) {
			/* The time is given in milliseconds. */
			time = strtonum(utf8str, 0, INT_MAX, &errstr);
			if (errstr == NULL) {
				msgwarn("strtonum");
				return (-1);
			}
			time = time % 1000 < 500 ? time/1000 : time/1000 + 1;
			tlen = asprintf(&tstr, "%d:%02d", (int)time/60,
			    (int)time % 60);
			if (tstr == NULL)
				fatal("malloc");
			msg(META_TIME, tstr, tlen);
			free(tstr);
		}
		else if (type == META_TRACKNO) {
			for (trcklen = 0; trcklen < strlen(utf8str);
			    trcklen++) {
				if (utf8str[trcklen] == '/')
					break;
				}
			msg(type, utf8str, trcklen);
		}
		else
			msg(type, utf8str, strlen(utf8str));
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
static int
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

/* Decode big-endian bytes. */
static size_t
be_to_uint(unsigned char *d)
{
	return ((d[0] << 24) + (d[1] << 16) + (d[2] << 8) + d[3]);
}

/* Decode little-endian bytes. */
static size_t
le_to_uint(unsigned char *d)
{
	return (d[0] + (d[1] << 8) + (d[2] << 16) + (d[3] << 24));
}
