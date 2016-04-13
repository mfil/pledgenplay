#ifndef PNP_H
#define PNP_H
/* parent_main return values */
enum {OK, INIT_FAIL, ERROR, CHILD_ERROR, SIGNAL};
/* output types */
enum {OUT_SNDIO, OUT_WAV_FILE, OUT_RAW};

int	child_main(int[2], int, int);
int	parent_main(int[2], pid_t, int *, int *);

struct meta	*get_meta(struct pollfd *, struct imsgbuf *);
void		free_meta(struct meta *);
int		decode(char *, char *, int);

struct meta {
	char		*artist;
	char		*title;
	char		*album;
	int		trackno; /* -1 means no track number given. */
	char		*date;
	char		*time;
};
#endif
