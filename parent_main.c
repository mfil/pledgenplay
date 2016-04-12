#include <sys/limits.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <imsg.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "comm.h"
#include "pnp.h"

void			child_warn(char *, size_t);
static void	 	signal_handler(int);
int 			check_child(pid_t, int *, int *);
__dead void		error(char *);

static struct imsgbuf	ibuf;
static int		term_sig, caught_sigchld;

int
parent_main(int sv[2], pid_t child, int *errval, int *child_status)
{
	int	rv;
	pid_t	wait_rv;

	signal(SIGCHLD, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	while (1) {
		if (caught_sigchld)
			if ((rv = check_child(child, errval, child_status)))
				return (rv == -1 ? ERROR : SIGNAL);
		if (term_sig) {
			*errval = term_sig;
			kill(child, SIGTERM);
			while ((wait_rv = waitpid(child, child_status, 0))
			    == -1) {
				if (errno != EINTR) {
					*errval = errno;
					return (ERROR);
				}
			}
			return (SIGNAL);
		}
	}
}

struct meta
*get_meta(struct pollfd *pfd, struct imsgbuf *ibuf)
{
	struct meta	*mdata = malloc(sizeof(struct meta));
	struct imsg	imsg;
	char		**field = NULL, *trackstr;
	const char	*err;
	size_t		len;
	int		nready;

	if (mdata == NULL)
		return (NULL);
	mdata->artist = NULL;
	mdata->title = NULL;
	mdata->album = NULL;
	mdata->trackno = -1;
	mdata->date = NULL;
	mdata->time = NULL;

	if (imsg_compose(ibuf, (u_int32_t)CMD_META, 0, getpid(), -1, NULL, 0)
	    == -1)
		goto fail;
	while (1) {
		nready = poll(pfd, 1, 0);
		if (nready == -1)
			goto fail;
		if (pfd->revents & POLLOUT) {
			if (imsg_flush(ibuf) == -1)
				goto fail;
			break;
		}
	}
	while (1) {
		nready = poll(pfd, 1, 0);
		if (nready == -1)
			goto fail;
		if (pfd->revents & (POLLIN|POLLHUP)) {
			if (imsg_read(ibuf) == -1 && errno != EAGAIN)
				goto fail;
			while (imsg_get(ibuf, &imsg) > 0) {
				field = NULL;
				switch ((int)imsg.hdr.type) {
				case (META_ARTIST):
					field = &mdata->artist;
					break;
				case (META_TITLE):
					field = &mdata->title;
					break;
				case (META_ALBUM):
					field = &mdata->album;
					break;
				case (META_TRACKNO):
					/* Store the track number as an int. */
					len = imsg.hdr.len - IMSG_HEADER_SIZE;
					trackstr = strndup(imsg.data, len);
					if (trackstr == NULL)
						goto fail;
					mdata->trackno = (int)strtonum(trackstr,
					    0, INT_MAX, &err);
					if (err != NULL)
						mdata->trackno = -1;
					free(trackstr);
					break;
				case (META_DATE):
					field = &mdata->date;
					break;
				case (META_TIME):
					field = &mdata->time;
					break;
				case (MSG_WARN):
					len = imsg.hdr.len - IMSG_HEADER_SIZE;
					child_warn(imsg.data, len);
					break;
				case (META_END):
					goto done;
				case (MSG_FILE_ERR):
					goto fail;
				}
				if (field != NULL && imsg.data != NULL) {
					len = imsg.hdr.len - IMSG_HEADER_SIZE;
					*field = strndup(imsg.data, len);
					if (*field == NULL)
						goto fail;
				}
				imsg_free(&imsg);
			}
		}
	}

done:
	imsg_free(&imsg);
	return (mdata);

fail:
	free_meta(mdata);
	return (NULL);
}

void
free_meta(struct meta *mdata)
{
	if (mdata != NULL) {
		free(mdata->artist);
		free(mdata->title);
		free(mdata->album);
		free(mdata->date);
		free(mdata->time);
		free(mdata);
	}
}

void
child_warn(char *msg, size_t len)
{
	msg[len] = '\0';
	printf("pnp child: warning: %s\n", msg);
}

void
signal_handler(int s)
{
	switch (s) {
	case (SIGCHLD):
		caught_sigchld = 1;
		break;
	case (SIGHUP):
	case (SIGINT):
	case (SIGTERM):
		term_sig = s;
		break;
	}
}

/* check_child: Return 1 if child has exited, 0 if not and -1 on error. */
int
check_child(pid_t child, int *errval, int *status)
{
	pid_t	rv;

	while ((rv = waitpid(child, status, WNOHANG)) == -1) {
		if (errno != EINTR) {
			*errval = errno;
			return (-1);
		}
	}
	*errval = SIGCHLD;
	return (rv == child ? 1 : 0);
}
