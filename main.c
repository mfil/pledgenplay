#include <sys/socket.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "pnp.h"
#include "parent.h"

int
main(int argc, char **argv)
{
	int		opt, out_fd, decflag = 0, sv[2];
	pid_t		child_pid;
	char		*ofile = NULL;

	extern char	*optarg;
	extern int	optind;

	while ((opt = getopt(argc, argv, "do:")) != -1) {
		switch (opt) {
		case 'd':
			decflag = 1;
			break;
		case 'o':
			if (asprintf(&ofile, "%s", optarg) < 0)
				err(1, "asprintf");
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (!decflag)
		return (0);
	if (argc == 0)
		errx(1, "no input file given");
	if (decflag && ofile == NULL)
		errx(1, "must specify output filename");
	if ((out_fd = open(ofile, O_WRONLY|O_CREAT|O_TRUNC,
	    S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)) == -1)
		err(1, "open");

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	if (child_pid == 0)
		child_main(sv, OUT_WAV_FILE, out_fd);
	else {
		parent_init(sv, child_pid);
		if (decode(argv[0]) != 0)
			errx(1, "decode");
		stop_child();
	}
	return (0);
}
