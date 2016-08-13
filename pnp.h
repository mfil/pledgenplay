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

int	child_main(int[2], int, int);
int	parent_main(int[2], pid_t, int *, int *);
int	send_new_file(char *, struct pollfd *, struct imsgbuf *);

struct meta	*get_meta(struct pollfd *, struct imsgbuf *);
void		free_meta(struct meta *);
int		decode(char *, struct pollfd *, struct imsgbuf *);

struct meta {
	char		*artist;
	char		*title;
	char		*album;
	int		trackno; /* -1 means no track number given. */
	char		*date;
	char		*time;
};
#endif
