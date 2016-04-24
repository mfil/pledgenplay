#ifndef PNP_FLAC_H
#define PNP_FLAC_H
#include "child.h"

struct flac_client_data {
	union out	out;
	u_int64_t	samples;
	unsigned int	rate, channels;
	int		error;
};

int	extract_meta_flac(FILE *);
int	init_decoder_flac(FILE *, struct output *);
int	decode_flac(void);
int	free_decoder_flac(void);
#endif
