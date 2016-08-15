#ifndef PNP_FLAC_H
#define PNP_FLAC_H
#include <FLAC/stream_decoder.h> /* For FLAC__StreamDecoderErrorStatus */

#include "child.h"

struct flac_client_data {
	struct input			*in;
	struct state			*state;
	struct output			*out;
	u_int64_t			samples;
	unsigned int			rate, channels;
	int				error;
	FLAC__StreamDecoderErrorStatus	error_status;
};

int	play_flac(struct input *, struct output *, struct state *);
int	extract_meta_flac(struct input *);
#endif
