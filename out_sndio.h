#ifndef PNP_OUT_SNDIO_H
#define PNP_OUT_SNDIO_H

#include <FLAC/format.h>

struct sample_buf {
	char		*buf;
	unsigned int	bps, channels;
	size_t		framesize;
	size_t		size, free; /* In frames. */
	size_t		rpos, wpos;
};

struct sample_buf	*sbuf_new(unsigned int, unsigned int, size_t);
void 			sbuf_free(struct sample_buf *);
void 			sbuf_clear(struct sample_buf *);
size_t			sbuf_put(struct sample_buf *,
			    const FLAC__int32 *const [], size_t);
int			sbuf_sio_write(struct sample_buf *, struct sio_hdl *);

#endif
