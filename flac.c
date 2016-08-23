#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <FLAC/format.h>
#include <FLAC/callback.h>
#include <FLAC/stream_decoder.h>

#include "comm.h"
#include "child.h"
#include "file.h"
#include "flac.h"
#include "pnp.h"

static FLAC__StreamDecoder	*init_flac_decoder(struct flac_client_data *);
static void			cleanup_flac_decoder(FLAC__StreamDecoder *);
static size_t			blocksize(unsigned char *);
static u_int64_t		get_samples(unsigned char *);
static u_int64_t		get_rate(unsigned char *);
static void			flac_error_msg(FLAC__StreamDecoderErrorStatus);

void mdata_cb(const FLAC__StreamDecoder *, const FLAC__StreamMetadata *,
    void *);
FLAC__StreamDecoderReadStatus read_cb(const FLAC__StreamDecoder *,
    FLAC__byte *, size_t *, void *);
FLAC__StreamDecoderWriteStatus write_cb_file_raw (const FLAC__StreamDecoder *,
    const FLAC__Frame *, const FLAC__int32 *const [], void *);
void err_cb (const FLAC__StreamDecoder *, const FLAC__StreamDecoderErrorStatus,
    void *);

static FLAC__StreamDecoder *
init_flac_decoder(struct flac_client_data *cdata)
{
	FLAC__StreamDecoderWriteCallback	write_cb;
	FLAC__StreamDecoder			*dec;

	switch (cdata->out->type) {
	case (OUT_RAW):
		write_cb = write_cb_file_raw;
		break;
	default:
		msgwarnx("invalid output type\n");
		return (NULL);
	}
	if ((dec = FLAC__stream_decoder_new()) == NULL)
		fatal("malloc");
	if (FLAC__stream_decoder_init_stream(dec, read_cb, NULL, NULL, NULL,
	    NULL, write_cb, mdata_cb, err_cb, cdata)
	    != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		msgwarnx("flac decoder: initialization failed");
		return (NULL);
	}
	return (dec);
}

FLAC__StreamDecoderReadStatus
read_cb(const FLAC__StreamDecoder *dec, FLAC__byte *buf, size_t *len,
    void *client_data)
{
	struct flac_client_data	*cdata = client_data;
	struct input		*in = cdata->in;
	struct state		*state = cdata->state;
	size_t			bytes_left, size, r_pos, w_pos, to_read, to_end;

	state->callback = 1;
	process_events(in, state);
	state->callback = 0;

	if (in->error)
		return (FLAC__STREAM_DECODER_READ_STATUS_ABORT);
	size = in->buf_size;
	bytes_left = size - in->buf_free;
	if (bytes_left < *len)
		*len = bytes_left;
	if (bytes_left == 0 && in->eof)
		return (FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM);
	r_pos = in->read_pos;
	w_pos = in->write_pos;
	to_read = *len;
	if (to_read == 0) {
		msgwarnx("input buffer is empty\n");
		return (FLAC__STREAM_DECODER_READ_STATUS_CONTINUE);
	}
	if (r_pos + to_read <= size) {
		memcpy(buf, in->buf + r_pos, to_read);
	}
	else {
		to_end = size - r_pos;
		memcpy(buf, in->buf + r_pos, to_end);
		memcpy(buf + to_end, in->buf, to_read - to_end);
	}
	in->read_pos = (r_pos + to_read) % size;
	in->buf_free += to_read;
	return (FLAC__STREAM_DECODER_READ_STATUS_CONTINUE);
}

void
mdata_cb(const FLAC__StreamDecoder *dec, const FLAC__StreamMetadata *mdata,
    void *client_data)
{
	struct flac_client_data	*cd;

	cd = (struct flac_client_data *)client_data;
	if (mdata->type == FLAC__METADATA_TYPE_STREAMINFO) {
		cd->samples = mdata->data.stream_info.total_samples;
		cd->rate = mdata->data.stream_info.sample_rate;
		cd->channels = mdata->data.stream_info.channels;
	}
}

FLAC__StreamDecoderWriteStatus
write_cb_file_raw(const FLAC__StreamDecoder *dec, const FLAC__Frame *frame,
    const FLAC__int32 *const decoded_samples[], void *client_data)
{
	struct flac_client_data	*cdata;
	FILE			*outfp;
	unsigned int		channels, bsiz, bps, chan, samp;
	char			*sample;

	cdata = (struct flac_client_data *)client_data;
	outfp = cdata->out->out.fp;
	bsiz = frame->header.blocksize;
	bps = frame->header.bits_per_sample/8;
	channels = frame->header.channels;
	for (samp = 0; samp < bsiz; samp++)
		for (chan = 0; chan < channels; chan++) {
			sample = (char *)(&decoded_samples[chan][samp]);
			if (fwrite(sample, bps, 1, outfp) < 1) {
				return
				    (FLAC__STREAM_DECODER_WRITE_STATUS_ABORT);
			}
		}
	return (FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE);
}

FLAC__StreamDecoderWriteStatus
write_cb_file_wav(const FLAC__StreamDecoder *dec, const FLAC__Frame *frame,
    const FLAC__int32 *const decoded_samples[], void *client_data)
{
}

void
err_cb(const FLAC__StreamDecoder *dec,
    const FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	struct flac_client_data	*cd;

	cd = (struct flac_client_data *)client_data;
	cd->error = 1;
	cd->error_status = status;
}

int
play_flac(struct input *in, struct output *out, struct state *state)
{
	struct flac_client_data		cdata;
	FLAC__StreamDecoder		*dec;

	state->play = PLAYING;
	state->callback = 0;
	cdata.in = in;
	cdata.state = state;
	cdata.out = out;
	cdata.error = 0;
	cdata.bytes_written = 0;
	if ((dec = init_flac_decoder(&cdata)) == NULL)
		return (-1);
	if (FLAC__stream_decoder_process_until_end_of_metadata(dec) == false) {
		if (cdata.error)
			flac_error_msg(cdata.error_status);
		cleanup_flac_decoder(dec);
		return (-1);
	}
	if (out->type == OUT_WAV_FILE)
		//cdata.bytes_written += write_wav_header(FILE *f, &cdata);
		;

	while (1) {
		process_events(in, state);
		switch (state->play) {
		case (PLAYING):
			if (FLAC__stream_decoder_process_single(dec) == false) {
				if (cdata.error)
					flac_error_msg(cdata.error_status);
				cleanup_flac_decoder(dec);
				return (-1);
			}
			if (FLAC__stream_decoder_get_state(dec)
			    == FLAC__STREAM_DECODER_END_OF_STREAM) {
				fclose(out->out.fp);
				msg(MSG_DONE, NULL, 0);
				return(0);
			}
			break;
		case (STOPPED):
			cleanup_flac_decoder(dec);
			return (0);
		case (PAUSED):
			break;
		default:
			fatal("unknown state");
		}
	}
}

static void
cleanup_flac_decoder(FLAC__StreamDecoder *dec)
{
	if (!(FLAC__stream_decoder_finish(dec)))
		msgwarnx("flac decoder: bad MD5 checksum\n");
	FLAC__stream_decoder_delete(dec);
}

int
extract_meta_flac(struct input *in)
{
	off_t		prev_pos;
	unsigned char	mdata_hdr[4], str_info[34], id3_hdr[10], *mdata = NULL;
	size_t		len;
	u_int64_t	rate, samples, t; /* t = time in s (samples/rate) */
	char		*t_str;
	int		fd, rv, id3v2_found = 0;

	fd = in->fd;
	/* Memorize position in file. */
	if ((prev_pos = lseek(fd, 0, SEEK_CUR)) == -1) {
		file_err(in, "lseek");
		return (-1);
	}
	if (lseek(fd, 0, SEEK_SET) < 0) {
		file_err(in, "fseek");
		return (-1);
	}
	/* Check if the file starts with an ID3v2 tag. */
	if (read(fd, id3_hdr, sizeof(id3_hdr)) < sizeof(id3_hdr)) {
		file_err(in, "read");
		return (-1);
	}
	if (memcmp(id3_hdr, "ID3", 3) == 0) {
		len = (id3_hdr[6] << 21) + (id3_hdr[7] << 14) +
		    (id3_hdr[8] << 7) + id3_hdr[9];
		if ((mdata = malloc(len)) == NULL)
			fatal("malloc");
		if (read(fd, mdata, len) < len) {
			file_err(in, "read");
			return (-1);
		}
		if ((rv = parse_id3v2(id3_hdr[5], mdata, len)) == -1) {
			file_errx(in, "malformed ID3v2 tag.");
			return (-1);
		}
		id3v2_found = 1;
	}
	else if (lseek(fd, 0, SEEK_SET) < 0) {
		file_err(in, "lseek");
		return (-1);
	}
	/*
	 * Read the STREAMINFO block. We do this even if an ID3v2 tag was found
	 * in order to calculate the time.
	 */
	if (lseek(fd, 4, SEEK_CUR) < 0) {
		file_err(in, "lseek");
		return (-1);
	}
	if (read(fd, mdata_hdr, sizeof(mdata_hdr)) < sizeof(mdata_hdr)) {
		file_err(in, "read");
		return (-1);
	}
	if ((mdata_hdr[0] & 0x7f) != 0 || blocksize(mdata_hdr) !=
	    sizeof(str_info)) {
		/*
		 * The file does not start with a valid STREAMINFO block,
		 * invalid flac file.
		 */
		file_errx(in, "missing STREAMINFO block.");
		return (-1);
	}
	/* Read the STREAMINFO block and calculate the running time. */
	if (read(fd, str_info, sizeof(str_info)) < sizeof(str_info)) {
		file_err(in, "read");
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
		if (read(fd, mdata_hdr, sizeof(mdata_hdr))
		    < sizeof(mdata_hdr)) {
			file_err(in, "read");
			return (-1);
		}
		if ((mdata_hdr[0] & 0x7f) == 4) {
			/* Found a VORBIS_COMMENT block. */
			len = blocksize(mdata_hdr);
			if ((mdata = malloc(len)) == NULL)
				fatal("malloc");
			if (read(fd, mdata, len) < len) {
				file_err(in, "read");
				return (-1);
			}
			/* Return to the previous position in the file. */
			if (lseek(fd, prev_pos, SEEK_SET) < 0) {
				file_err(in, "lseek");
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
			if (lseek(fd, prev_pos, SEEK_SET) < 0) {
				file_err(in, "fseek");
				return (-1);
			}
			return (0);
		}
		else if (lseek(fd, blocksize(mdata_hdr), SEEK_CUR) < 0) {
			file_err(in, "lseek");
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

static void
flac_error_msg(FLAC__StreamDecoderErrorStatus error_status)
{
	switch (error_status) {
	case (FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC):
		msgwarnx("flac decoder: lost sync");
		break;
	case (FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER):
		msgwarnx("flac decoder: bad header");
		break;
	case (FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH):
		msgwarnx("flac decoder: CRC mismatch");
		break;
	case (FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM):
		msgwarnx("flac decoder: unparseable stream");
		break;
	default:
		msgwarnx("flac decoder: unknown error");
	}
}
