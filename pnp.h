#ifndef PNP_H
#define PNP_H
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <imsg.h>
#include <poll.h>

/* parent_main return values */
enum {OK, INIT_FAIL, ERROR, CHILD_ERROR, SIGNAL};
/* output types */
enum {NONE, OUT_SNDIO, OUT_WAV_FILE, OUT_RAW};

struct meta {
	char		*artist;
	char		*title;
	char		*album;
	int		trackno; /* -1 means no track number given. */
	char		*date;
	char		*time;
};

int	child_main(int[2], int, int);

void		free_meta(struct meta *);
int		decode(char *);
#endif
