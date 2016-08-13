#ifndef PNP_FLAC_H
#define PNP_FLAC_H
#include <FLAC/stream_decoder.h> /* For FLAC__StreamDecoderErrorStatus */

#include "child.h"

struct flac_client_data {
	int				in_fd;
	union out			out;
	u_int64_t			samples;
	unsigned int			rate, channels;
	int				error;
	FLAC__StreamDecoderErrorStatus	error_status;
};

int	extract_meta_flac(int);
int	init_decoder_flac(int, struct output *);
int	decode_flac(void);
int	cleanup_flac_decoder(void);
#endif
