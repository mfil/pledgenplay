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
void		parent_init(int[2], pid_t);
void		child_warn(char *, size_t);
int 		check_child(void);
__dead void	parent_err(const char *);
ssize_t		parent_process_events(struct imsg *);
int		send_new_file(char *);
void		parent_msg(int, char *, size_t);
struct meta	*get_meta(void);
void		stop_child(void);
#endif
