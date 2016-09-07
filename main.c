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

#include <sys/socket.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pnp.h"

extern char	*__progname;

int
main(int argc, char **argv)
{
	struct out	out;

	int		opt, decflag = 0, rawflag = 0, sv[2];
	FILE		*outfp;
	pid_t		child_pid;
	char		*ofile = NULL, *infile = NULL, *base = NULL,
			*ext = NULL;

	extern char	*optarg;
	extern int	optind;

	while ((opt = getopt(argc, argv, "do:r")) != -1) {
		switch (opt) {
		case 'd':
			decflag = 1;
			break;
		case 'o':
			if (asprintf(&ofile, "%s", optarg) < 0)
				err(1, "asprintf");
			break;
		case 'r':
			rawflag = 1;
			break;
		default:
			(void)fprintf(stderr,
			    "usage: %s [-dr] [-o output_file] file\n",
			    __progname);
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (!decflag)
		errx(1, "Only decoding to files is implemented so far.");
	if (argc == 0)
		errx(1, "no input file given");
	infile = argv[0];
	if (decflag && ofile == NULL) {
		/* No output filename specified. */
		size_t	osize;
		osize = strlen(infile) + 5;
		ofile = malloc(osize);
		if (ofile == NULL)
			err(1, "malloc");
		strlcpy(ofile, infile, osize);
		base = basename(ofile);
		if (base == NULL)
			err(1, "basename");
		ext = strrchr(base, '.');
		if (ext == base) {
			/*
			 * The only dot in the filename is at the beginning,
			 * so it is a hidden file without file extension.
			 */
			ext = NULL;
		}
		if (ext == NULL)
			strlcat(ofile, rawflag ? ".raw" : ".wav", osize);
		else if (strcmp(ext, rawflag ? ".raw" : ".wav") == 0) {
			/*
			 * The file extension of the output file is the
			 * same as that of the input file.
			 */
			errx(1, "Input file ends in %s. You must specify an "
			    "output filename using the -o option.", ext);
		}
		else {
			ext = strrchr(ofile, '.');
			if (ext == NULL)
				errx(1, "I didn't think that this can happen.");
			*ext = '\0';
			strlcat(ofile, rawflag ? ".raw" : ".wav", osize);
		}
	}
	if ((outfp = fopen(ofile, "w")) == NULL)
		err(1, "open");

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	if (child_pid == 0) {
		out.type = rawflag ? OUT_RAW : OUT_WAV_FILE;
		out.handle.fp = outfp;
		child_main(sv, &out);
	}
	else {
		parent_init(sv, child_pid);
		if (decode(argv[0]) != 0)
			errx(1, "decode");
		stop_child();
	}
	return (0);
}
