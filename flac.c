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

#include <fcntl.h>
#include <sndio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <FLAC/format.h>
#include <FLAC/callback.h>
#include <FLAC/stream_decoder.h>

#include "comm.h"
#include "child.h"
#include "child_errors.h"
#include "child_messages.h"
#include "file.h"
#include "flac.h"
#include "out_sndio.h"
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
FLAC__StreamDecoderWriteStatus write_cb_file (const FLAC__StreamDecoder *,
    const FLAC__Frame *, const FLAC__int32 *const [], void *);
FLAC__StreamDecoderWriteStatus write_cb_sndio (const FLAC__StreamDecoder *,
    const FLAC__Frame *, const FLAC__int32 *const [], void *);
void err_cb (const FLAC__StreamDecoder *, const FLAC__StreamDecoderErrorStatus,
    void *);

static FLAC__StreamDecoder *
init_flac_decoder(struct flac_client_data *cdata)
{
	FLAC__StreamDecoderWriteCallback	write_cb;
	FLAC__StreamDecoder			*dec;

	switch (cdata->out->type) {
	case (OUT_SNDIO):
		write_cb = write_cb_sndio;
		break;
	case (OUT_WAV_FILE):
	case (OUT_RAW):
		write_cb = write_cb_file;
		break;
	default:
		child_warnx("invalid output type\n");
		return (NULL);
	}
	if ((dec = FLAC__stream_decoder_new()) == NULL)
		child_fatal("malloc");
	if (FLAC__stream_decoder_init_stream(dec, read_cb, NULL, NULL, NULL,
	    NULL, write_cb, mdata_cb, err_cb, cdata)
	    != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		child_warnx("flac decoder: initialization failed");
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
	process_events(in, cdata->out, state);
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
		child_warnx("input buffer is empty\n");
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
		cd->bps = mdata->data.stream_info.bits_per_sample;
		cd->max_bsize = mdata->data.stream_info.max_blocksize;
		if (cd->max_bsize == 0)
			/*
			 * Maximal blocksize unknown. Use the maximal size
			 * allowed by the standard.
			 */
			cd->max_bsize = 1 << 16;
	}
}

FLAC__StreamDecoderWriteStatus
write_cb_file(const FLAC__StreamDecoder *dec, const FLAC__Frame *frame,
    const FLAC__int32 *const decoded_samples[], void *client_data)
{
	struct flac_client_data	*cdata;
	FILE			*outfp;
	unsigned int		channels, bsiz, bps, chan, samp;
	const FLAC__int32	*sample;

	cdata = (struct flac_client_data *)client_data;
	outfp = cdata->out->handle.fp;
	bsiz = frame->header.blocksize;
	bps = frame->header.bits_per_sample/8;
	channels = frame->header.channels;
	for (samp = 0; samp < bsiz; samp++)
		for (chan = 0; chan < channels; chan++) {
			sample = &decoded_samples[chan][samp];
			if (fwrite(sample, bps, 1, outfp) < 1) {
				return
				    (FLAC__STREAM_DECODER_WRITE_STATUS_ABORT);
			}
			cdata->bytes_written += bps;
		}
	return (FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE);
}

FLAC__StreamDecoderWriteStatus
write_cb_sndio(const FLAC__StreamDecoder *dec, const FLAC__Frame *frame,
    const FLAC__int32 *const decoded_samples[], void *client_data)
{
	struct flac_client_data	*cdata;
	size_t			nput;

	cdata = (struct flac_client_data *)client_data;
	if (frame->header.bits_per_sample != cdata->bps)
		child_fatalx("FLAC files with variable bps are not supported.");
	if (frame->header.channels != cdata->channels)
		child_fatalx("FLAC files with a variable number of channels are"
		    " not supported.");
	nput = sbuf_put(cdata->sbuf, decoded_samples, frame->header.blocksize);
	if (nput < frame->header.blocksize)
		child_fatalx("Sample buffer full.");
	return (FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE);
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
play_flac(struct input *in, struct out *out, struct state *state)
{
	struct flac_client_data		cdata;
	struct sio_par			par;
	FLAC__StreamDecoder		*dec;
	int				decode_done = 0;

	state->play = PLAYING;
	state->callback = 0;
	cdata.in = in;
	cdata.state = state;
	cdata.out = out;
	cdata.error = 0;
	cdata.bytes_written = 0;
	cdata.sbuf = NULL;
	if ((dec = init_flac_decoder(&cdata)) == NULL)
		return (-1);
	if (FLAC__stream_decoder_process_until_end_of_metadata(dec) == false) {
		if (cdata.error)
			flac_error_msg(cdata.error_status);
		cleanup_flac_decoder(dec);
		return (-1);
	}
	/* If the output is to a file, we just decode in one go. */
	if (out->type == OUT_WAV_FILE || out->type == OUT_RAW) {
		if (out->type == OUT_WAV_FILE) {
			if (cdata.samples > UINT32_MAX)
				return (-1);
			cdata.bytes_written += write_wav_header(out->handle.fp,
			    cdata.channels, cdata.rate, cdata.bps,
			    cdata.samples);
		}
		if (FLAC__stream_decoder_process_until_end_of_stream(dec)
		    == false) {
			if (cdata.error)
				flac_error_msg(cdata.error_status);
			cleanup_flac_decoder(dec);
			return (-1);
		}
		if (out->type == OUT_WAV_FILE) {
			/* Check if we need a padding byte for WAVE. */
			if (out->type == OUT_WAV_FILE
			    && cdata.bytes_written % 2 != 0
			    && fwrite("\0", 1, 1, out->handle.fp) < 1)
				return (-1);
		}
		if (fclose(out->handle.fp))
			child_warn("fclose");
		cleanup_flac_decoder(dec);
		enqueue_message(MSG_DONE, "");
		return (0);
	}

	/* We decode to a sndio device. First, it needs to be configured. */
	sio_initpar(&par);
	par.bits = cdata.bps;
	par.bps = SIO_BPS(cdata.bps);
	par.sig = 1;
	par.le = SIO_LE_NATIVE;
	par.pchan = cdata.channels;
	par.rate = cdata.rate;
	par.appbufsz = (cdata.rate * 200) / 1000; /* 200 ms buffer */
	par.xrun = SIO_IGNORE;
	if (sio_setpar(out->handle.sio, &par) == 0)
		child_fatalx("sio_setpar: failed");
	/*
	 * Now check if the parameters were set correctly.
	 * According to sio_open(3), a difference of 0.5% in the rate
	 * should be negligible.
	 */
	if (sio_getpar(out->handle.sio, &par) == 0)
		child_fatal("sio_getpar");
	if (par.bits != cdata.bps || par.bps != cdata.bps/8
	    || par.sig != 1 || par.le != 1
	    || par.pchan != cdata.channels || par.xrun != SIO_IGNORE
	    || par.appbufsz != (cdata.rate * 200) / 1000
	    || par.rate < (995*cdata.rate)/1000
	    || par.rate > (1005*cdata.rate)/1000) {
		child_fatalx("setting sndio parameters failed");
	}
	/* Prepare the buffer for the samples. */
	size_t	sbuf_size;
	if (par.appbufsz > cdata.max_bsize)
		sbuf_size = 3*par.appbufsz;
	else
		sbuf_size = 3*cdata.max_bsize;
	/*
	 * Audio devices process frames not one by one, but in blocks.
	 * This blocksize is stored in par.round. According to www.sndio.org,
	 * rounding sbuf_size to a multiple of par.round is optimal.
	 */
	sbuf_size += par.round - 1;
	sbuf_size = sbuf_size - (sbuf_size % par.round);
	cdata.sbuf = sbuf_new(cdata.bps/8, cdata.channels, sbuf_size);
	if (cdata.sbuf == NULL)
		child_fatal("calloc");
	if (sio_start(out->handle.sio) == 0)
		child_fatalx("sio_start: failed\n");

	while (1) {
		process_events(in, out, state);
		switch (state->play) {
		case (RESUME):
			state->play = PLAYING;
			if (out->type == OUT_SNDIO &&
			    sio_start(out->handle.sio) == 0)
				child_fatalx("sio_start: failed\n");
			/* Fallthrough */
		case (PLAYING):
			/* sndio output */
			if (out->type == OUT_SNDIO && out->ready &&
			    sbuf_sio_write(cdata.sbuf, out->handle.sio)) {
				child_fatalx("sio_write: failed");
			}
			if (decode_done
			    && cdata.sbuf->free == cdata.sbuf->size) {
				sio_stop(out->handle.sio);
				cleanup_flac_decoder(dec);
				enqueue_message(MSG_DONE, "");
				return (0);
			}
			if (!decode_done
			    && cdata.sbuf->free >= cdata.max_bsize) {
				if (FLAC__stream_decoder_process_single(dec)
				    == false) {
					if (cdata.error)
						flac_error_msg(cdata.error_status);
					cleanup_flac_decoder(dec);
					return (-1);
				}
			}
			if (FLAC__stream_decoder_get_state(dec)
			    == FLAC__STREAM_DECODER_END_OF_STREAM) {
				decode_done = 1;
			}
			break;
		case (PAUSING):
			/*
			 * If there's space available, decode another block and
			 * put it in the buffer.
			 */
			if (out->type == OUT_SNDIO &&
			    sio_stop(out->handle.sio) == 0)
				child_fatalx("sio_stop: failed\n");
			state->play = PAUSED;
			/* Fallthrough */
		case (PAUSED):
			break;
		case (STOPPED):
			if (out->type == OUT_SNDIO &&
			    sio_stop(out->handle.sio) == 0)
				child_fatalx("sio_stop: failed\n");
			cleanup_flac_decoder(dec);
			return (0);
		default:
			child_fatal("unknown state");
		}
	}
}

static void
cleanup_flac_decoder(FLAC__StreamDecoder *dec)
{
	if (!(FLAC__stream_decoder_finish(dec)))
		child_warnx("flac decoder: bad MD5 checksum\n");
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
			child_fatal("malloc");
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
		enqueue_message(META_TIME, "?");
	else {
		int	len;
		t = samples/rate;
		len = asprintf(&t_str, "%u:%02u", (unsigned int)(t/60),
		    (unsigned int)(t % 60)); /* m:s */
		if (len == -1)
			child_fatal("malloc");
		enqueue_message(META_TIME, t_str);
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
				child_fatal("malloc");
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
		child_warnx("flac decoder: lost sync");
		break;
	case (FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER):
		child_warnx("flac decoder: bad header");
		break;
	case (FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH):
		child_warnx("flac decoder: CRC mismatch");
		break;
	case (FLAC__STREAM_DECODER_ERROR_STATUS_UNPARSEABLE_STREAM):
		child_warnx("flac decoder: unparseable stream");
		break;
	default:
		child_warnx("flac decoder: unknown error");
	}
}
