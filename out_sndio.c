#include <fcntl.h>
#include <unistd.h>
#include <sndio.h>
#include <stdlib.h>
#include <string.h>

#include <FLAC/format.h>

#include "child.h"
#include "out_sndio.h"

static void	_sbuf_put_nowrap(struct sample_buf *,
		    const FLAC__int32 *const [], size_t, size_t);

struct sample_buf *
sbuf_new(unsigned int bps, unsigned int channels, size_t nframes)
{
	struct sample_buf	*sbuf;

	sbuf = malloc(sizeof(struct sample_buf));
	if (sbuf == NULL)
		return (NULL);
	sbuf->buf = reallocarray(NULL, bps*channels, nframes);
	if (sbuf->buf == NULL) {
		free(sbuf);
		return (NULL);
	}
	sbuf->channels = channels;
	sbuf->bps = bps;
	sbuf->framesize = bps*channels;
	sbuf->size = sbuf->free = nframes;
	sbuf->rpos = sbuf->wpos = 0;

	return (sbuf);
}

void
sbuf_free(struct sample_buf *sbuf)
{
	free(sbuf->buf);
	free(sbuf);
}

void
sbuf_clear(struct sample_buf *sbuf)
{
	sbuf->free = sbuf->size;
	sbuf->rpos = sbuf->wpos = 0;
}

size_t
sbuf_put(struct sample_buf *sbuf, const FLAC__int32 *const smp[],
    size_t nframes)
{
	size_t		to_end;

	if (sbuf->free < nframes)
		nframes = sbuf->free;
	if (nframes == 0)
		return (0);
	to_end = sbuf->size - sbuf->wpos;
	if (nframes <= to_end) {
		_sbuf_put_nowrap(sbuf, smp, 0, nframes);
	}
	else {
		_sbuf_put_nowrap(sbuf, smp, 0, to_end);
		_sbuf_put_nowrap(sbuf, smp, to_end, nframes);
	}

	return (nframes);
}

/*
 * _sbuf_put_nowrap: Put the samples between start (inclusive) and end
 * (exclusive) into the buffer. The caller has to guarantee that there is
 * enough space in the buffer and that it does not wrap around.
 */
static void
_sbuf_put_nowrap(struct sample_buf *sbuf, const FLAC__int32 *const smp[],
    size_t start, size_t end)
{
	size_t			chan, frame;
	char			*buf;
	const FLAC__int32	*sample;

	buf = sbuf->buf + sbuf->wpos * sbuf->framesize;
	for (frame = start; frame < end; frame++)
	for (chan = 0; chan < sbuf->channels; chan++) {
		sample = &smp[chan][frame];
		memcpy(buf, sample, sbuf->bps);
		buf += sbuf->bps;
	}
	sbuf->free -= end - start;
	sbuf->wpos = (sbuf->wpos + end - start) % sbuf->size;
}

int
sbuf_sio_write(struct sample_buf *sbuf, struct sio_hdl *hdl)
{
	size_t	to_end, nframes, bytes_written, frames_written;
	char	*buf;

	nframes = sbuf->size - sbuf->free;
	to_end = sbuf->size - sbuf->rpos;

	if (to_end < nframes) {
		buf = sbuf->buf + sbuf->rpos * sbuf->framesize;
		bytes_written = sio_write(hdl, buf, to_end*sbuf->framesize);
		if (bytes_written == 0 && sio_eof(hdl))
			return (-1);
		if (bytes_written % sbuf->framesize != 0)
			return (-1);
		frames_written = bytes_written/sbuf->framesize;
		sbuf->free += frames_written;
		sbuf->rpos = (sbuf->rpos + frames_written) % sbuf->size;
		return (0);
	}
	buf = sbuf->buf + sbuf->rpos * sbuf->framesize;
	bytes_written = sio_write(hdl, buf, nframes*sbuf->framesize);
	if (bytes_written == 0 && sio_eof(hdl))
		return (-1);
	if (bytes_written % sbuf->framesize != 0)
		return (-1);
	frames_written = bytes_written/sbuf->framesize;
	sbuf->free += frames_written;
	sbuf->rpos = (sbuf->rpos + frames_written) % sbuf->size;
	return (0);
}
