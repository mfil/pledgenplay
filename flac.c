#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <FLAC/callback.h>
#include <FLAC/stream_decoder.h>

#include "comm.h"
#include "file.h"
#include "flac.h"

static size_t			blocksize(unsigned char *);
static u_int64_t		get_samples(unsigned char *);
static u_int64_t		get_rate(unsigned char *);

static FLAC__StreamDecoder	*dec;

int
init_decoder_flac(int out_format)
{
	if ((dec = FLAC__stream_decoder_new()) == NULL)
		return (-1);
	return (-1);
}

int
extract_meta_flac(FILE *f)
{
	long		prev_pos;
	unsigned char	mdata_hdr[4], str_info[34], id3_hdr[10], *mdata = NULL;
	size_t		len;
	u_int64_t	rate, samples, t; /* t = time in s (samples/rate) */
	char		*t_str;
	int		rv, id3v2_found;

	/* Memorize position in file. */
	if ((prev_pos = ftell(f)) == -1) {
		file_err("ftell");
		return (-1);
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		file_err("fseek");
		return (-1);
	}
	/* Check if the file starts with an ID3v2 tag. */
	if (fread(id3_hdr, sizeof(id3_hdr), 1, f) < 1) {
		file_err("fread");
		return (-1);
	}
	if (memcmp(id3_hdr, "ID3", 3) == 0) {
		len = (id3_hdr[6] << 21) + (id3_hdr[7] << 14) +
		    (id3_hdr[8] << 7) + id3_hdr[9];
		if ((mdata = malloc(len)) == NULL)
			fatal("malloc");
		if (fread(mdata, len, 1, f) < 1) {
			file_err("fread");
			return (-1);
		}
		if ((rv = parse_id3v2(id3_hdr[5], mdata, len)) == -1) {
			file_errx("malformed ID3v2 tag.");
			return (-1);
		}
		id3v2_found = 1;
	}
	else if (fseek(f, 0, SEEK_SET) != 0) {
		file_err("fseek");
		return (-1);
	}
	/*
	 * Read the STREAMINFO block. We do this even if an ID3v2 tag was found
	 * in order to calculate the time.
	 */
	if (fseek(f, 4, SEEK_CUR) != 0) {
		file_err("fseek");
		return (-1);
	}
	if (fread(mdata_hdr, sizeof(mdata_hdr), 1, f) < 1) {
		file_err("fread");
		return (-1);
	}
	if ((mdata_hdr[0] & 0x7f) != 0 || blocksize(mdata_hdr) !=
	    sizeof(str_info)) {
		/*
		 * The file does not start with a valid STREAMINFO block,
		 * invalid flac file.
		 */
		file_errx("missing STREAMINFO block.");
		return (-1);
	}
	/* Read the STREAMINFO block and calculate the running time. */
	if (fread(str_info, sizeof(str_info), 1, f) < 1) {
		file_err("fread");
		return (-1);
	}
	rate = get_rate(str_info);
	if (rate > 655350 || rate == 0)
		return (-1);
	samples = get_samples(str_info);
	if (samples == 0)
		/* 0 samples means an unknown number. */
		msg(META_TIME, "?", 1);
	else {
		int	len;
		t = samples/rate;
		len = asprintf(&t_str, "%u:%02u", (unsigned int)(t/60),
		    (unsigned int)(t % 60)); /* m:s */
		if (len == -1)
			fatal("malloc");
		msg(META_TIME, t_str, len);
		free(t_str);
	}
	if (id3v2_found)
		goto done;

	/* Look for the VORBIS_COMMENT block. */
	while (1) {
		if (fread(mdata_hdr, sizeof(mdata_hdr), 1, f) < 1) {
			file_err("fread");
			return (-1);
		}
		if ((mdata_hdr[0] & 0x7f) == 4) {
			/* Found a VORBIS_COMMENT block. */
			len = blocksize(mdata_hdr);
			if ((mdata = malloc(len)) == NULL)
				fatal("malloc");
			if (fread(mdata, len, 1, f) < 1) {
				file_err("fread");
				return (-1);
			}
			/* Return to the previous position in the file. */
			if (fseek(f, prev_pos, SEEK_SET) != 0) {
				file_err("fseek");
				return (-1);
			}
			rv = parse_vorbis_comment(mdata, len);
			goto done;
		}
		else if (mdata_hdr[0] & 0x80) {
			/*
			 * The header of the last metadata block has the first
			 * bit set to 1, so this is the last block, and we
			 * have not found a VORBIS_COMMENT block.
			 */
			if (fseek(f, prev_pos, SEEK_SET) == -1) {
				file_err("fseek");
				return (-1);
			}
			return (0);
		}
		else if (fseek(f, blocksize(mdata_hdr), SEEK_CUR) != 0) {
			file_err("fseek");
			return (-1);
		}
	}

done:
	free(mdata);
	return (rv);
}

/* Extract the size of a metadata block from its header. */
static size_t
blocksize(unsigned char *mdata_hdr)
{
	return ((mdata_hdr[1] << 16) + (mdata_hdr[2] << 8) + mdata_hdr[3]);
}

static u_int64_t
get_rate(unsigned char *strinf)
{
	/* The rate is given by bits 80-99. */
	return ((strinf[10] << 12) + (strinf[11] << 4) + (strinf[12] >> 4));
}

static u_int64_t
get_samples(unsigned char *strinf)
{
	u_int64_t	rv;

	/* The number of samples is given by bits 108-143. */
	rv = ((u_int64_t)(strinf[13] & 0x0f) << 32) + (strinf[14] << 24) +
	    (strinf[15] << 16) + (strinf[16] << 8) + strinf[17];
	return (rv);
}
