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
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <curses.h>
#include <err.h>
#include <fcntl.h>
#include <imsg.h>
#include <libgen.h>
#include <poll.h>
#include <sndio.h>
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
	struct sio_hdl	*hdl;

	int		opt, decflag = 0, rawflag = 0, sv[2];
	FILE		*outfp;
	pid_t		child_pid;
	char		*name = NULL, *infile = NULL, *base = NULL,
			*ext = NULL;
	
	char		default_dev[] = "snd/0";

	extern char	*optarg;
	extern int	optind;

	while ((opt = getopt(argc, argv, "do:r")) != -1) {
		switch (opt) {
		case 'd':
			decflag = 1;
			break;
		case 'o':
			if (asprintf(&name, "%s", optarg) < 0)
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

	if (argc == 0)
		errx(1, "no input file given");
	infile = argv[0];
	if (decflag) {
		if (name == NULL) {
			/* No output filename specified. */
			size_t	osize;
			osize = strlen(infile) + 5;
			name = malloc(osize);
			if (name == NULL)
				err(1, "malloc");
			strlcpy(name, infile, osize);
			base = basename(name);
			if (base == NULL)
				err(1, "basename");
			ext = strrchr(base, '.');
			if (ext == base) {
				/*
			 	* The only dot in the filename is at the
				* beginning, so it is a hidden file without
				* file extension.
			 	*/
				ext = NULL;
			}
			if (ext == NULL)
				strlcat(name, rawflag ? ".raw" : ".wav",
				    osize);
			else if (strcmp(ext, rawflag ? ".raw" : ".wav") == 0) {
				/*
			 	* The file extension of the output file is the
			 	* same as that of the input file.
			 	*/
				errx(1, "Input file ends in %s. Specify an  "
			    	"output filename with -o.", ext);
			}
			else {
				ext = strrchr(name, '.');
				if (ext == NULL)
					errx(1, "This can't happen.");
				*ext = '\0';
				strlcat(name, rawflag ? ".raw" : ".wav",
				    osize);
			}
		}
		if ((outfp = fopen(name, "w")) == NULL)
			err(1, "open");
		out.type = rawflag ? OUT_RAW : OUT_WAV_FILE;
		out.handle.fp = outfp;
	}
	else { /* decflag == 0 */
		if (name == NULL)
			name = default_dev;
		hdl = sio_open(name, SIO_PLAY, 1);
		if (hdl == NULL)
			errx(1, "sio_open: failed");
		out.type = OUT_SNDIO;
		out.handle.sio = hdl;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	if (child_pid == 0) {
		child_main(sv, &out);
	}
	else {
		parent_init(sv, child_pid);
		if (decflag && decode(argv[0]) != 0)
			errx(1, "decode");
		else {
			struct pollfd	pfd;
			int		nready, paused = 0;

			pfd.fd = 1;
			pfd.events = POLLIN;
			if (start_play(argv[0]) != 0) 
				errx(1, "start_play");
			initscr();
			cbreak();
			noecho();
			while (1) {
				if ((nready = poll(&pfd, 1, 1)) < 0)
					parent_err("poll");
				if (nready == 1 && pfd.revents & POLLIN &&
				    getchar() == ' ') {
					if (paused) {
						paused = 0;
						resume_play();
					} else {
						paused = 1;
						pause_play();
					}
				}
			}
		}
		stop_child();
	}
	return (0);
}
