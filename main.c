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

int
main(int argc, char **argv)
{
	int		opt, out_fd, decflag = 0, rawflag = 0, sv[2];
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
	if ((out_fd = open(ofile, O_WRONLY|O_CREAT|O_TRUNC,
	    S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)) == -1)
		err(1, "open");

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	if (child_pid == 0)
		child_main(sv, rawflag ? OUT_RAW : OUT_WAV_FILE, out_fd);
	else {
		parent_init(sv, child_pid);
		if (decode(argv[0]) != 0)
			errx(1, "decode");
		stop_child();
	}
	return (0);
}
