#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <err.h>
#include <errno.h>
#include <imsg.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "comm.h"
#include "file.h"
#include "flac.h"
#include "pnp.h"

static void	new_file(int);
static int	extract_meta(void);

static struct imsgbuf	ibuf;
static struct {
	FILE	*f;
	int	fmt;
} infile;

int
child_main(int sv[2], int output_type, int out_fd)
{
	struct pollfd	pfd;
	struct imsg	imsg;

	ssize_t		nbytes;
	int		nready;

	if (pledge("stdio recvfd", NULL) == -1)
		return (-1);
	close(0);
	close(1);
	close(sv[0]);
	imsg_init(&ibuf, sv[1]);
	pfd.fd = sv[1];
	pfd.events = POLLIN|POLLOUT;

	while (1) {
		nready = poll(&pfd, 1, 0);
		if (nready == -1) {
			_err("poll");
		}
		if (pfd.revents & (POLLIN|POLLHUP)) {
			if (imsg_read(&ibuf) == -1 && errno != EAGAIN)
				_err("imsg_read");
			while ((nbytes = imsg_get(&ibuf, &imsg)) > 0) {
				switch ((int)imsg.hdr.type) {
				case (NEW_FILE):
					new_file(imsg.fd);
					break;
				case (CMD_META):
					if (infile.f == NULL)
						file_err();
					else
						extract_meta();
					break;
				case (CMD_EXIT):
					_exit(0);
				case (CMD_PLAY):
					close(out_fd);
				}
				imsg_free(&imsg);
			}
		if (pfd.revents & POLLOUT)
			if (imsg_flush(&ibuf) == -1)
				_err("imsg_flush");
		}
	}
}

static void
new_file(int fd)
{
	char	msg_cannot_read[] = "Unable to read the file.";
	char	msg_unknown[] = "Unknown file format.";

	/* Close the old file (if there was one). */
	if (infile.f != NULL) {
		if (fclose(infile.f) != 0)
			msgstr(MSG_WARN, "error closing input file.");
		infile.f = NULL;
		infile.fmt = UNKNOWN;
	}
	/* Open the new one. */
	if ((infile.f = fdopen(fd, "rb")) == NULL) {
		file_err();
		return;
	}
	/* Determine the file format. */
	if ((infile.fmt = filetype(infile.f)) == -1 || infile.fmt == UNKNOWN)
		file_err();
	else
		msg(MSG_ACK_FILE, NULL, 0);
}

static int
extract_meta(void)
{
	int	rv;

	switch (infile.fmt) {
	case (FLAC):
		rv = extract_meta_flac(infile.f);
		break;
	default:
		rv = -1;
	}
	if (rv == -1) {
		file_err();
		return (-1);
	}
	msg(META_END, NULL, 0);
	return (rv);
}

/*
 * Compose a message to the parent. The message will be truncated if it exceeds
 * the maximum length for imsg_compose (64 kb).
 */
void
msg(int type, void *data, size_t len)
{
	if (len > UINT16_MAX)
		len = UINT16_MAX;
	if (imsg_compose(&ibuf, (u_int32_t)type, 0, getpid(), -1, data, len)
	    == -1)
		_err("imsg");
}

/* Send msg to the parent; msg must be null-terminated. */
void
msgstr(int type, char *msg)
{
	if (imsg_compose(&ibuf, (u_int32_t)type, 0, getpid(), -1, msg,
	    strlen(msg)+1) == -1)
		_err("imsg");
}

void
file_err(void)
{
	if (infile.f != NULL && fclose(infile.f) != 0)
		msgstr(MSG_WARN, "error closing input file.");
	infile.f = NULL;
	infile.fmt = UNKNOWN;
	if (imsg_compose(&ibuf, (u_int32_t)MSG_FILE_ERR, 0, getpid(), -1, NULL,
	    0) == -1)
		_err("imsg");
	return;
}

__dead void
fatal(char *msg)
{
	char	*errstr;
	int	len;

	len = asprintf(&errstr, "%s: %s", msg, strerror(errno));
	if (errstr == NULL)
		_err("malloc");
	if (imsg_compose(&ibuf, (u_int32_t)MSG_FATAL, 0, getpid(), -1, errstr,
	    len+1) == -1 || imsg_flush(&ibuf) == -1)
		_err("imsg");
	_exit(1);
}

/*
 * If the child process cannot communicate with the parent anymore, we try to
 * write an error message to stderr and exit.
 */
__dead void
_err(char *msg)
{
	dprintf(2, "pnp child: %s: %s\n", msg, strerror(errno));
	_exit(1);
}
