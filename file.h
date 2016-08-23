#ifndef PNP_FILE_H
#define PNP_FILE_H

#define ID3_HDR_LEN	10

int	filetype(int);
int	parse_id3v2(unsigned char, unsigned char *, ssize_t);
int	parse_vorbis_comment(unsigned char *, ssize_t);
int	write_wav_header(FILE *, unsigned int, unsigned int, unsigned int,
	    uint64_t);

enum {UNKNOWN, FLAC, MP3, WAVE_PCM}; /* Return values for filetype. */
#endif
